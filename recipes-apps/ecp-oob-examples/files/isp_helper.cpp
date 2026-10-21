// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * v4l2_helper.cpp
 * Video4Linux2 (V4L2) helper functions for camera capture and video device management.
 * Author: LT Thomas
 */

#include "isp_helper.h"

using namespace std;
using namespace std::chrono;



// Constructor
V4L2Camera::V4L2Camera() : fd(-1), buffers(nullptr), n_buffers(0), width(1920), height(1080), 
                           pixel_format(0), bytes_per_line(0), use_dmabuf_import(true) {
    // Try to use DMABUF import with CMA buffers for physically contiguous memory
    // Falls back to MMAP if not available
}

// Destructor
V4L2Camera::~V4L2Camera() {
    stop();
}

// Helper function to verify memory contiguity
bool V4L2Camera::verifyMemoryContiguity(int buffer_index) {
    // For MMAP buffers, check if driver provides contiguous allocation
    // This is driver-dependent and may not always be detectable
    cout << "Buffer " << buffer_index << " uses MMAP (contiguity driver-dependent)" << endl;
    return false; // Cannot guarantee contiguity with MMAP
}

// Export buffer as DMABUF for hardware device sharing
int V4L2Camera::exportBufferAsDmabuf(int buffer_index) {
    if (fd == -1 || buffer_index >= n_buffers || !buffers) {
        cerr << "Invalid buffer index or device not opened" << endl;
        return -1;
    }
    
    // Check if we already have a DMABUF fd for this buffer
    if (buffers[buffer_index].dmabuf_fd > 0) {
        return buffers[buffer_index].dmabuf_fd;
    }
    
    struct v4l2_exportbuffer expbuf;
    memset(&expbuf, 0, sizeof(expbuf));
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = buffer_index;
    expbuf.plane = 0;  // For NV12
    expbuf.flags = O_RDWR;  // Allow read/write access
    
    if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) == -1) {
        cerr << "VIDIOC_EXPBUF failed for buffer " << buffer_index 
             << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
        return -1;
    }
    
    buffers[buffer_index].dmabuf_fd = expbuf.fd;
    
    cout << "Exported buffer " << buffer_index << " as DMABUF fd: " 
         << expbuf.fd << endl;
    
    return expbuf.fd;
}

// Get DMABUF fd for a specific buffer (export if not already done)
int V4L2Camera::getDmabufFd(int buffer_index) {
    if (buffer_index >= n_buffers || !buffers) {
        return -1;
    }
    
    if (buffers[buffer_index].dmabuf_fd <= 0) {
        return exportBufferAsDmabuf(buffer_index);
    }
    
    return buffers[buffer_index].dmabuf_fd;
}


// Set the target return period (in milliseconds) used when pace == true.
void V4L2Camera::setTargetPeriodMs(double ms) {
    if (ms <= 0.0) {
        frame_period = std::chrono::nanoseconds(0);
    } else {
        frame_period = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double, std::milli>(ms));
    }
    pacing_started = false; // restart the pacing grid on (re)configuration
    pacing_primed = false;  // re-run the warm-up after (re)configuration
}

// When pacing, let the driver queue this many extra frames before the first read.
void V4L2Camera::setPacingPrimeFrames(int frames) {
    pacing_prime_frames = (frames > 0) ? frames : 0;
    pacing_primed = false; // allow the warm-up to run again
}

// Attach a PCIe frame-sharing bridge and the mailbox channel this camera uses.
void V4L2Camera::setEpProxy(EpProxyBridge* bridge, int channel) {
    ep_bridge_ = bridge;
    ep_channel_ = channel;
    ep_setup_done_ = false; // (re)run channel registration + phys discovery
}

// Register the channel and discover each buffer's physical DDR address so the
// bridge can publish them later. Runs its work only once, and must be called
// before VIDIOC_STREAMON (the phys-discovery scan briefly stamps a magic
// pattern into every buffer, which is fine while they hold no frame data).
void V4L2Camera::epProxySetup() {
    if (!ep_bridge_ || ep_channel_ < 0 || ep_setup_done_)
        return;
    if (!ep_bridge_->isReady() || !buffers || n_buffers == 0)
        return;

    uint32_t frame_size = (uint32_t)(width * height * 3 / 2); // NV12
    ep_bridge_->registerChannel(ep_channel_, (uint32_t)width, (uint32_t)height,
                                pixel_format, bytes_per_line, frame_size);

    for (unsigned int i = 0; i < n_buffers; ++i) {
        if (buffers[i].start == nullptr || buffers[i].start == MAP_FAILED)
            continue;
        ep_bridge_->discoverPhys(ep_channel_, (int)i,
                                 buffers[i].start, buffers[i].length);
    }
    ep_setup_done_ = true;
}

bool V4L2Camera::captureFrameDmabufNV12(cv::Mat& frame_nv12,
                                        struct timeval* out_timestamp,
                                        bool pace) {
    // Default the returned timestamp to zero in case of early return.
    if (out_timestamp != nullptr) {
        out_timestamp->tv_sec = 0;
        out_timestamp->tv_usec = 0;
    }

    // Check if device is properly opened
    if (fd == -1) {
        cerr << "Device not opened - call open() first" << endl;
        return false;
    }
    
    // Check if streaming was started
    if (!buffers || n_buffers == 0) {
        cerr << "Buffers not initialized - call startStreaming() first" << endl;
        return false;
    }
    
    // One-time warm-up: when pacing, let the driver capture and queue extra
    // frames before we read the first image so there is a buffering cushion to
    // absorb later jitter. This waits before the first VIDIOC_DQBUF, allowing
    // the driver's done-queue to fill (bounded by the number of queued buffers).
    if (pace && frame_period.count() > 0 && pacing_prime_frames > 0 && !pacing_primed) {
        std::this_thread::sleep_for(frame_period * pacing_prime_frames);
        pacing_primed = true;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = use_dmabuf_import ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
    //cout << "sizeof(buf) = " << sizeof(buf) << " bytes" << endl;

    
    // Retry mechanism for EAGAIN with timeout
    int retry_count = 0;
    const int max_retries = 100; // Allow up to 100ms wait
    
    while (retry_count < max_retries) {
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            break; // Successfully got a buffer
        }
        
        if (errno == EAGAIN) {
            usleep(1000); // Wait 1ms before retrying
            retry_count++;
            continue;
        }
        
        // Real error occurred
        cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "Device fd: " << fd << ", buffers: " << (buffers ? "valid" : "null") << ", n_buffers: " << n_buffers << endl;
        return false;
    }
    
    if (retry_count >= max_retries) {
        // Timeout - no frame available after waiting
        return false;
    }

    // Return the driver-provided Start-Of-Frame timestamp for this buffer.
    if (out_timestamp != nullptr) {
        *out_timestamp = buf.timestamp;
    }

    // Publish this frame over PCIe (non-blocking) before we touch it locally.
    if (ep_bridge_ && ep_setup_done_) {
        ep_bridge_->publishFrame(ep_channel_, (int)buf.index,
                                 buffers[buf.index].start,
                                 (uint32_t)(width * height * 3 / 2));
    }

    const int rows_nv12 = height * 3 / 2;
    const size_t nv12_size = static_cast<size_t>(width) * rows_nv12;

    // Ensure destination Mat is correctly sized/typed for packed NV12.
    if (frame_nv12.empty() ||
        frame_nv12.rows != rows_nv12 ||
        frame_nv12.cols != width ||
        frame_nv12.type() != CV_8UC1) {
        frame_nv12.create(rows_nv12, width, CV_8UC1);
    }

    void* src = buffers[buf.index].start;
    if (src == nullptr || src == MAP_FAILED) {
        cerr << "captureFrameDmabufNV12: buffer index " << buf.index
             << " has no CPU mapping" << endl;
        return false;
    }

    size_t to_copy = nv12_size;
    if (buf.bytesused > 0 && static_cast<size_t>(buf.bytesused) < to_copy) {
        to_copy = static_cast<size_t>(buf.bytesused);
    }
    memcpy(frame_nv12.data, src, to_copy);

    // Re-queue the buffer AFTER processing is complete
    struct v4l2_buffer qbuf;
    memset(&qbuf, 0, sizeof(qbuf));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    qbuf.memory = use_dmabuf_import ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    
    if (use_dmabuf_import) {
        // For DMABUF import, specify the DMABUF fd and length
        qbuf.m.fd = buffers[buf.index].dmabuf_fd;
        qbuf.length = buffers[buf.index].length;
    }
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }

    // Optional software pacing: hold the return until the next deadline so this
    // function returns on a fixed period grid. Uses an accumulating deadline to
    // avoid cumulative drift, and resyncs if processing fell behind.
    if (pace && frame_period.count() > 0) {
        auto now = std::chrono::steady_clock::now();
        if (!pacing_started) {
            next_return_deadline = now + frame_period;
            pacing_started = true;
        } else {
            std::this_thread::sleep_until(next_return_deadline);
            next_return_deadline += frame_period;
            auto after = std::chrono::steady_clock::now();
            if (next_return_deadline < after) {
                next_return_deadline = after + frame_period; // fell behind: resync
            }
        }
    }

    return true;
}
// Capture frame and get DMABUF fd only (no OpenCV processing)
bool V4L2Camera::captureFrameDmabuf( MlppHelper& mlpp_helper_in) {
    // Check if device is properly opened
    if (fd == -1) {
        cerr << "Device not opened - call open() first" << endl;
        return false;
    }
    
    // Check if streaming was started
    if (!buffers || n_buffers == 0) {
        cerr << "Buffers not initialized - call startStreaming() first" << endl;
        return false;
    }
    
    // One-time warm-up: when pacing, let the driver capture and queue extra
    // frames before we read the first image so there is a buffering cushion to
    // absorb later jitter. This waits before the first VIDIOC_DQBUF, allowing
    // the driver's done-queue to fill (bounded by the number of queued buffers).
    if (frame_period.count() > 0 && pacing_prime_frames > 0 && !pacing_primed) {
        std::this_thread::sleep_for(frame_period * pacing_prime_frames);
        pacing_primed = true;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = use_dmabuf_import ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
    //cout << "sizeof(buf) = " << sizeof(buf) << " bytes" << endl;

    
    // Retry mechanism for EAGAIN with timeout
    int retry_count = 0;
    const int max_retries = 100; // Allow up to 100ms wait
    
    while (retry_count < max_retries) {
        if (ioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
            break; // Successfully got a buffer
        }
        
        if (errno == EAGAIN) {
            usleep(1000); // Wait 1ms before retrying
            retry_count++;
            continue;
        }
        
        // Real error occurred
        cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "Device fd: " << fd << ", buffers: " << (buffers ? "valid" : "null") << ", n_buffers: " << n_buffers << endl;
        return false;
    }
    
    if (retry_count >= max_retries) {
        // Timeout - no frame available after waiting
        return false;
    }
    
    // Publish this frame over PCIe (non-blocking) before we touch it locally.
    if (ep_bridge_ && ep_setup_done_) {
        ep_bridge_->publishFrame(ep_channel_, (int)buf.index,
                                 buffers[buf.index].start,
                                 (uint32_t)(width * height * 3 / 2));
    }

    // Export buffer as DMABUF
    int dmabuf_fd;
    if (use_dmabuf_import) {
        // For CMA buffers, DMABUF fd is already available (physically contiguous)
        dmabuf_fd = buffers[buf.index].dmabuf_fd;
    } else {
        // For MMAP buffers, export as DMABUF
        dmabuf_fd = getDmabufFd(buf.index);
    }


    // Process the frame using DMABUF (zero-copy to MLPP driver)
    // With CMA buffers, this should now have correct data (physically contiguous)
    // Calculate NV12 buffer size: width * height * 1.5 (Y plane + UV plane)
    size_t nv12_buffer_size = width * height * 3 / 2;
    mlpp_helper_in.write_dmabuf_to_mlpp(dmabuf_fd, nv12_buffer_size);

    
    // Re-queue the buffer AFTER processing is complete
    struct v4l2_buffer qbuf;
    memset(&qbuf, 0, sizeof(qbuf));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    qbuf.memory = use_dmabuf_import ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    
    if (use_dmabuf_import) {
        // For DMABUF import, specify the DMABUF fd and length
        qbuf.m.fd = buffers[buf.index].dmabuf_fd;
        qbuf.length = buffers[buf.index].length;
    }
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }
    
    // Optional software pacing: hold the return until the next deadline so this
    // function returns on a fixed period grid. Uses an accumulating deadline to
    // avoid cumulative drift, and resyncs if processing fell behind.
    if (frame_period.count() > 0) {
        auto now = std::chrono::steady_clock::now();
        if (!pacing_started) {
            next_return_deadline = now + frame_period;
            pacing_started = true;
        } else {
            std::this_thread::sleep_until(next_return_deadline);
            next_return_deadline += frame_period;
            auto after = std::chrono::steady_clock::now();
            if (next_return_deadline < after) {
                next_return_deadline = after + frame_period; // fell behind: resync
            }
        }
    }
    
    return dmabuf_fd;
}

// Helper function to check for MMAP memory support
bool V4L2Camera::checkMmapMemorySupport() {
    // MMAP is always supported by V4L2 devices
    cout << "Device uses MMAP (standard V4L2 memory allocation)" << endl;
    return true;
}

// Helper function to set custom stride padding
bool V4L2Camera::setCustomStride(uint32_t padding_bytes) {
    if (fd == -1) {
        cerr << "Device not opened, cannot set custom stride" << endl;
        return false;
    }
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    // Get current format
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        cerr << "VIDIOC_G_FMT failed in setCustomStride: " << strerror(errno) << endl;
        return false;
    }
    
    // Calculate new stride with padding
    uint32_t min_bytes_per_line = fmt.fmt.pix.width; // NV12 Y plane = 1 byte per pixel
    uint32_t new_stride = min_bytes_per_line + padding_bytes;
    
    cout << "Setting custom stride: " << new_stride 
         << " (width=" << fmt.fmt.pix.width 
         << ", min_stride=" << min_bytes_per_line 
         << ", padding=" << padding_bytes << ")" << endl;
    
    // Set new stride
    fmt.fmt.pix.bytesperline = new_stride;
    fmt.fmt.pix.sizeimage = new_stride * fmt.fmt.pix.height;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        cerr << "VIDIOC_S_FMT failed setting custom stride: " << strerror(errno) << endl;
        return false;
    }
    
    // Update our internal values
    bytes_per_line = fmt.fmt.pix.bytesperline;
    
    cout << "Custom stride set successfully. Actual stride: " << bytes_per_line << endl;
    
    if (bytes_per_line != new_stride) {
        cout << "WARNING: Driver adjusted stride from " << new_stride 
             << " to " << bytes_per_line << endl;
    }
    
    return true;
}

bool V4L2Camera::open(const char* device) {
    cout << "Attempting to open device: " << device << endl;
    
    // Add a small delay to help with potential timing issues when opening multiple cameras
    usleep(100000); // 100ms delay
    
    // Use blocking I/O to avoid EAGAIN errors - comment out O_NONBLOCK if you want blocking behavior
    fd = ::open(device, O_RDWR); // | O_NONBLOCK);
    if (fd == -1) {
        cerr << "Cannot open device " << device << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
        
        // Check if device exists
        if (access(device, F_OK) == -1) {
            cerr << "Device " << device << " does not exist" << endl;
        } else if (access(device, R_OK | W_OK) == -1) {
            cerr << "No read/write permission for device " << device << endl;
        }
        
        return false;
    }
    
    cout << "Successfully opened device " << device << " with fd: " << fd << endl;
    
    // Query capabilities
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        cerr << "VIDIOC_QUERYCAP failed: " << strerror(errno) << endl;
        close();
        return false;
    }
    
    cout << "Device capabilities: 0x" << hex << cap.capabilities << dec << endl;
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        cerr << "Device does not support streaming" << endl;
        close();
        return false;
    }
    
    // Check if device supports multi-plane API
    bool supports_mplane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    bool supports_splane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    
    cout << "Device supports: " 
         << (supports_splane ? "single-plane " : "")
         << (supports_mplane ? "multi-plane" : "") << endl;
    
    // Enumerate supported formats
    cout << "Enumerating supported formats:" << endl;
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int fmt_index = 0;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        char fourcc_fmt[5];
        fourcc_fmt[0] = fmtdesc.pixelformat & 0xFF;
        fourcc_fmt[1] = (fmtdesc.pixelformat >> 8) & 0xFF;
        fourcc_fmt[2] = (fmtdesc.pixelformat >> 16) & 0xFF;
        fourcc_fmt[3] = (fmtdesc.pixelformat >> 24) & 0xFF;
        fourcc_fmt[4] = '\0';
        cout << "  Format " << fmt_index << ": " << fourcc_fmt 
             << " - " << fmtdesc.description << endl;
        fmtdesc.index++;
        fmt_index++;
    }
    
    // Get default format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        cerr << "VIDIOC_G_FMT failed: " << strerror(errno) << endl;
        close();
        return false;
    }
    
    // Display current format information
    cout << "Default format: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height 
         << ", pixelformat: 0x" << hex << fmt.fmt.pix.pixelformat << dec;
    
    // Convert pixel format to readable string
    char fourcc[5];
    fourcc[0] = fmt.fmt.pix.pixelformat & 0xFF;
    fourcc[1] = (fmt.fmt.pix.pixelformat >> 8) & 0xFF;
    fourcc[2] = (fmt.fmt.pix.pixelformat >> 16) & 0xFF;
    fourcc[3] = (fmt.fmt.pix.pixelformat >> 24) & 0xFF;
    fourcc[4] = '\0';
    cout << " (" << fourcc << ")" << endl;
    
    // Display buffer information
    cout << "bytesperline=" << fmt.fmt.pix.bytesperline 
         << ", sizeimage=" << fmt.fmt.pix.sizeimage << endl;
    
    // Update dimensions from device
    width = fmt.fmt.pix.width;
    height = fmt.fmt.pix.height;
    pixel_format = fmt.fmt.pix.pixelformat;
    
    // Try to set NV12 format using the appropriate API based on device capabilities
    bool use_mplane = supports_mplane && !supports_splane;
    
    cout << "Setting format to NV12 at " << width << "x" << height << " using " 
         << (use_mplane ? "multi-plane" : "single-plane") << " API..." << endl;
    
    if (use_mplane) {
        // Use multi-plane API
        struct v4l2_format fmt_mp;
        memset(&fmt_mp, 0, sizeof(fmt_mp));
        fmt_mp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt_mp.fmt.pix_mp.width = width;  // Use device's current resolution
        fmt_mp.fmt.pix_mp.height = height;
        fmt_mp.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt_mp.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt_mp.fmt.pix_mp.num_planes = 1;  // NV12 as single contiguous plane
        fmt_mp.fmt.pix_mp.plane_fmt[0].bytesperline = width;
        fmt_mp.fmt.pix_mp.plane_fmt[0].sizeimage = width * height * 3 / 2;
        
        if (ioctl(fd, VIDIOC_S_FMT, &fmt_mp) == -1) {
            cerr << "Failed to set NV12 format: " << strerror(errno) << endl;
            close();
            return false;
        }
        
        // Update our values
        width = fmt_mp.fmt.pix_mp.width;
        height = fmt_mp.fmt.pix_mp.height;
        pixel_format = fmt_mp.fmt.pix_mp.pixelformat;
        bytes_per_line = fmt_mp.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        // Use single-plane API
        // First try the format to see what the driver will accept
        struct v4l2_format try_fmt;
        memset(&try_fmt, 0, sizeof(try_fmt));
        try_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try_fmt.fmt.pix.width = width;  // Use device's current resolution
        try_fmt.fmt.pix.height = height;
        try_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        try_fmt.fmt.pix.field = V4L2_FIELD_NONE;
        
        if (ioctl(fd, VIDIOC_TRY_FMT, &try_fmt) == -1) {
            cerr << "VIDIOC_TRY_FMT failed for NV12: " << strerror(errno) << endl;
            close();
            return false;
        }
        
        cout << "Driver accepted NV12 with: stride=" << try_fmt.fmt.pix.bytesperline 
             << ", sizeimage=" << try_fmt.fmt.pix.sizeimage << endl;
        
        // Now set the format - use the try_fmt structure directly
        if (ioctl(fd, VIDIOC_S_FMT, &try_fmt) == -1) {
            cerr << "Failed to set NV12 format: " << strerror(errno) << endl;
            cerr << "Trying with fresh format structure..." << endl;
            
            // Create a completely fresh format structure
            struct v4l2_format set_fmt;
            memset(&set_fmt, 0, sizeof(set_fmt));
            set_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            set_fmt.fmt.pix.width = width;  // Use device's current resolution
            set_fmt.fmt.pix.height = height;
            set_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
            set_fmt.fmt.pix.field = V4L2_FIELD_NONE;
            set_fmt.fmt.pix.bytesperline = try_fmt.fmt.pix.bytesperline;
            set_fmt.fmt.pix.sizeimage = try_fmt.fmt.pix.sizeimage;
            set_fmt.fmt.pix.colorspace = try_fmt.fmt.pix.colorspace;
            
            if (ioctl(fd, VIDIOC_S_FMT, &set_fmt) == -1) {
                cerr << "Failed to set NV12 format (second attempt): " << strerror(errno) << endl;
                close();
                return false;
            }
            
            // Update our values from set_fmt
            width = set_fmt.fmt.pix.width;
            height = set_fmt.fmt.pix.height;
            pixel_format = set_fmt.fmt.pix.pixelformat;
            bytes_per_line = set_fmt.fmt.pix.bytesperline;
        } else {
            // Update our values from try_fmt
            width = try_fmt.fmt.pix.width;
            height = try_fmt.fmt.pix.height;
            pixel_format = try_fmt.fmt.pix.pixelformat;
            bytes_per_line = try_fmt.fmt.pix.bytesperline;
        }
    }
    
    cout << "Successfully set NV12 format" << endl;
    
    // Display what was actually set
    char fourcc_set[5];
    fourcc_set[0] = pixel_format & 0xFF;
    fourcc_set[1] = (pixel_format >> 8) & 0xFF;
    fourcc_set[2] = (pixel_format >> 16) & 0xFF;
    fourcc_set[3] = (pixel_format >> 24) & 0xFF;
    fourcc_set[4] = '\0';
    
    cout << "Final format: " << width << "x" << height 
         << ", format: " << fourcc_set 
         << ", stride: " << bytes_per_line << endl;
    
    // Query and display current framerate
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
        if (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
            double fps = (double)parm.parm.capture.timeperframe.denominator / 
                        (double)parm.parm.capture.timeperframe.numerator;
            cout << "Camera framerate: " << fps << " fps ("
                 << parm.parm.capture.timeperframe.numerator << "/"
                 << parm.parm.capture.timeperframe.denominator << ")" << endl;
        } else {
            cout << "Camera framerate: not reported by driver" << endl;
        }
    } else {
        cerr << "VIDIOC_G_PARM failed: " << strerror(errno) << endl;
    }
    
    return initBuffers();
}

// Overloaded open() with desired resolution
bool V4L2Camera::open(const char* device, int desired_width, int desired_height) {
    cout << "Attempting to open device: " << device << " at " << desired_width << "x" << desired_height << endl;
    
    // Add a small delay to help with potential timing issues when opening multiple cameras
    usleep(100000); // 100ms delay
    
    // Use blocking I/O
    fd = ::open(device, O_RDWR);
    if (fd == -1) {
        cerr << "Cannot open device " << device << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
        
        if (access(device, F_OK) == -1) {
            cerr << "Device " << device << " does not exist" << endl;
        } else if (access(device, R_OK | W_OK) == -1) {
            cerr << "No read/write permission for device " << device << endl;
        }
        
        return false;
    }
    
    cout << "Successfully opened device " << device << " with fd: " << fd << endl;
    
    // Query capabilities
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        cerr << "VIDIOC_QUERYCAP failed: " << strerror(errno) << endl;
        close();
        return false;
    }
    
    cout << "Device capabilities: 0x" << hex << cap.capabilities << dec << endl;
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        cerr << "Device does not support streaming" << endl;
        close();
        return false;
    }
    
    // Check API support
    bool supports_mplane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    bool supports_splane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    
    cout << "Device supports: " 
         << (supports_splane ? "single-plane " : "")
         << (supports_mplane ? "multi-plane" : "") << endl;
    
    // Set the desired resolution to NV12 format
    bool use_mplane = supports_mplane && !supports_splane;
    
    cout << "Setting format to NV12 at " << desired_width << "x" << desired_height 
         << " using " << (use_mplane ? "multi-plane" : "single-plane") << " API..." << endl;
    
    if (use_mplane) {
        // Use multi-plane API
        struct v4l2_format fmt_mp;
        memset(&fmt_mp, 0, sizeof(fmt_mp));
        fmt_mp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt_mp.fmt.pix_mp.width = desired_width;
        fmt_mp.fmt.pix_mp.height = desired_height;
        fmt_mp.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt_mp.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt_mp.fmt.pix_mp.num_planes = 1;
        fmt_mp.fmt.pix_mp.plane_fmt[0].bytesperline = desired_width;
        fmt_mp.fmt.pix_mp.plane_fmt[0].sizeimage = desired_width * desired_height * 3 / 2;
        
        if (ioctl(fd, VIDIOC_S_FMT, &fmt_mp) == -1) {
            cerr << "Failed to set NV12 format at " << desired_width << "x" << desired_height 
                 << ": " << strerror(errno) << endl;
            close();
            return false;
        }
        
        width = fmt_mp.fmt.pix_mp.width;
        height = fmt_mp.fmt.pix_mp.height;
        pixel_format = fmt_mp.fmt.pix_mp.pixelformat;
        bytes_per_line = fmt_mp.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        // Use single-plane API with TRY_FMT first
        struct v4l2_format try_fmt;
        memset(&try_fmt, 0, sizeof(try_fmt));
        try_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try_fmt.fmt.pix.width = desired_width;
        try_fmt.fmt.pix.height = desired_height;
        try_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        try_fmt.fmt.pix.field = V4L2_FIELD_NONE;
        
        if (ioctl(fd, VIDIOC_TRY_FMT, &try_fmt) == -1) {
            cerr << "VIDIOC_TRY_FMT failed for NV12 at " << desired_width << "x" << desired_height 
                 << ": " << strerror(errno) << endl;
            close();
            return false;
        }
        
        cout << "Driver accepted NV12 with: " << try_fmt.fmt.pix.width << "x" << try_fmt.fmt.pix.height
             << ", stride=" << try_fmt.fmt.pix.bytesperline 
             << ", sizeimage=" << try_fmt.fmt.pix.sizeimage << endl;
        
        // Set the format
        if (ioctl(fd, VIDIOC_S_FMT, &try_fmt) == -1) {
            cerr << "Failed to set NV12 format: " << strerror(errno) << endl;
            close();
            return false;
        }
        
        width = try_fmt.fmt.pix.width;
        height = try_fmt.fmt.pix.height;
        pixel_format = try_fmt.fmt.pix.pixelformat;
        bytes_per_line = try_fmt.fmt.pix.bytesperline;
    }
    
    // Display what was set
    char fourcc_set[5];
    fourcc_set[0] = pixel_format & 0xFF;
    fourcc_set[1] = (pixel_format >> 8) & 0xFF;
    fourcc_set[2] = (pixel_format >> 16) & 0xFF;
    fourcc_set[3] = (pixel_format >> 24) & 0xFF;
    fourcc_set[4] = '\0';
    
    cout << "Final format: " << width << "x" << height 
         << ", format: " << fourcc_set 
         << ", stride: " << bytes_per_line << endl;
    
    // Query and display current framerate
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
        if (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
            double fps = (double)parm.parm.capture.timeperframe.denominator / 
                        (double)parm.parm.capture.timeperframe.numerator;
            cout << "Camera framerate: " << fps << " fps ("
                 << parm.parm.capture.timeperframe.numerator << "/"
                 << parm.parm.capture.timeperframe.denominator << ")" << endl;
        } else {
            cout << "Camera framerate: not reported by driver" << endl;
        }
    } else {
        cerr << "VIDIOC_G_PARM failed: " << strerror(errno) << endl;
    }
    
    return initBuffers();
}

// Allocate physically contiguous CMA buffer via DMA-BUF heap
int V4L2Camera::allocateCmaBuffer(size_t size) {
    // List available DMA heaps for debugging
    cout << "Available DMA heaps:" << endl;
    const char* all_heaps[] = {
        "/dev/dma_heap/shared_dma_mem_region",
        "/dev/dma_heap/linux,cma",
        "/dev/dma_heap/cma", 
        "/dev/dma_heap/system",
        "/dev/dma_heap/system-uncached",
        nullptr
    };
    for (int i = 0; all_heaps[i] != nullptr; i++) {
        if (access(all_heaps[i], F_OK) == 0) {
            cout << "  Found: " << all_heaps[i] << endl;
        }
    }
    
    // Try to open DMA-BUF CMA heap only (NOT system heap)
    // System heap is not physically contiguous and won't work with V4L2 hardware
    const char* heap_paths[] = {
        "/dev/dma_heap/shared_dma_mem_region",  // Platform-specific reserved DMA region
        "/dev/dma_heap/linux,cma",              // Standard CMA heap name
        "/dev/dma_heap/cma",                    // Alternative CMA heap name
        nullptr
    };
    
    int heap_fd = -1;
    const char* used_heap = nullptr;
    for (int i = 0; heap_paths[i] != nullptr; i++) {
        heap_fd = ::open(heap_paths[i], O_RDWR);
        if (heap_fd >= 0) {
            used_heap = heap_paths[i];
            cout << "Opened CMA/Reserved DMA-BUF heap: " << used_heap << endl;
            break;
        }
    }
    
    if (heap_fd < 0) {
        cerr << "Failed to open CMA DMA-BUF heap. CMA buffers not available." << endl;
        cerr << "Your system only has /dev/dma_heap/system which is NOT physically contiguous." << endl;
        cerr << "V4L2 hardware requires CMA heap (/dev/dma_heap/linux,cma or /dev/dma_heap/cma)." << endl;
        cerr << "Falling back to MMAP allocation." << endl;
        return -1;
    }
    
    // Allocate buffer from heap
    struct dma_heap_allocation_data heap_data;
    memset(&heap_data, 0, sizeof(heap_data));
    heap_data.len = size;
    heap_data.fd_flags = O_RDWR | O_CLOEXEC;
    heap_data.heap_flags = 0;
    
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data) < 0) {
        cerr << "DMA_HEAP_IOCTL_ALLOC failed: " << strerror(errno) << endl;
        ::close(heap_fd);
        return -1;
    }
    
    ::close(heap_fd);
    
    cout << "Allocated CMA buffer: fd=" << heap_data.fd << ", size=" << size << " bytes" << endl;
    return heap_data.fd;
}

// Initialize buffers using DMABUF import (CMA buffers)
bool V4L2Camera::initBuffersDmabufImport() {
    cout << "Attempting to initialize with DMABUF import (CMA buffers)..." << endl;
    
    // Calculate buffer size for NV12
    size_t buffer_size = width * height * 3 / 2;
    
    // First, check if device supports DMABUF import
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 3; //5
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        cerr << "Device does not support DMABUF import: " << strerror(errno) << endl;
        cerr << "Falling back to MMAP..." << endl;
        use_dmabuf_import = false;
        return false;
    }
    
    cout << "Device supports DMABUF import. Requesting " << req.count << " buffers." << endl;
    
    if (req.count < 2) {
        cerr << "Insufficient buffer memory" << endl;
        return false;
    }
    
    // Allocate buffer structures
    buffers = new V4L2Buffer[req.count];
    n_buffers = req.count;
    
    // Allocate CMA buffers for each V4L2 buffer
    for (unsigned int i = 0; i < n_buffers; ++i) {
        buffers[i].length = buffer_size;
        buffers[i].is_imported = true;
        
        // Allocate physically contiguous buffer via DMA-BUF heap
        buffers[i].dmabuf_fd = allocateCmaBuffer(buffer_size);
        
        if (buffers[i].dmabuf_fd < 0) {
            // Cleanup and fallback to MMAP
            cerr << "Failed to allocate CMA buffer " << i << ", falling back to MMAP" << endl;
            for (unsigned int j = 0; j < i; j++) {
                if (buffers[j].dmabuf_fd >= 0) {
                    ::close(buffers[j].dmabuf_fd);
                }
            }
            delete[] buffers;
            buffers = nullptr;
            n_buffers = 0;
            use_dmabuf_import = false;
            return false;
        }
        
        // Map the DMABUF for CPU access
        buffers[i].start = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, 
                               MAP_SHARED, buffers[i].dmabuf_fd, 0);
        
        if (buffers[i].start == MAP_FAILED) {
            cerr << "Failed to mmap CMA buffer " << i << ": " << strerror(errno) << endl;
            ::close(buffers[i].dmabuf_fd);
            
            // Cleanup
            for (unsigned int j = 0; j < i; j++) {
                if (buffers[j].start != MAP_FAILED) {
                    munmap(buffers[j].start, buffers[j].length);
                }
                ::close(buffers[j].dmabuf_fd);
            }
            delete[] buffers;
            buffers = nullptr;
            n_buffers = 0;
            use_dmabuf_import = false;
            return false;
        }
        
        cout << "Allocated CMA buffer " << i << ": dmabuf_fd=" << buffers[i].dmabuf_fd 
             << ", size=" << buffer_size << " bytes" << endl;
    }
    
    cout << "Successfully allocated " << n_buffers << " CMA buffers for DMABUF import" << endl;
    return true;
}

bool V4L2Camera::initBuffers() {
    // Try DMABUF import with CMA buffers first (for physically contiguous memory)
    if (use_dmabuf_import && initBuffersDmabufImport()) {
        cout << "Using DMABUF import with CMA buffers (physically contiguous)" << endl;
        return true;
    }
    
    // Fallback to MMAP if DMABUF import is not available
    cout << "Using MMAP memory type for V4L2 buffers" << endl;
    
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 3;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        cerr << "VIDIOC_REQBUFS failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        return false;
    }
    
    cout << "Requested " << req.count << " V4L2 buffers for NV12 capture using MMAP" << endl;
    
    if (req.count < 2) {
        cerr << "Insufficient buffer memory" << endl;
        return false;
    }
    
    buffers = new V4L2Buffer[req.count];
    n_buffers = req.count;
    
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            cerr << "VIDIOC_QUERYBUF failed: " << strerror(errno) << endl;
            return false;
        }
        
        buffers[i].length = buf.length;
        buffers[i].dmabuf_fd = -1;  // Initialize DMABUF fd as invalid
        buffers[i].is_imported = false;  // MMAP buffer, not imported
        
        // Use MMAP for V4L2 buffer allocation with optimized flags for video streaming
        // MAP_LOCKED: Lock pages in memory to prevent swapping (requires privileges)
        // MAP_POPULATE: Prefault pages to reduce page faults during streaming
        // MAP_HUGETLB: Use huge pages for better performance (if available)
        int mmap_flags = MAP_SHARED;
        
        // Try with performance optimization flags first
        mmap_flags |= MAP_POPULATE;  // Prefault pages to avoid page faults during capture
        
        // Uncomment if you have the privileges and want to lock pages in memory
        // mmap_flags |= MAP_LOCKED;  // Prevents swapping (requires CAP_IPC_LOCK)
        
        // Try huge pages for large buffers (typically 2MB+)
        if (buf.length >= (2 * 1024 * 1024)) {
            mmap_flags |= MAP_HUGETLB;
            cout << "Using huge pages for buffer " << i << " (size: " << buf.length << ")" << endl;
        }
        
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, 
                               mmap_flags, fd, buf.m.offset);
        
        // If huge pages failed, fallback to standard mapping
        if (buffers[i].start == MAP_FAILED && (mmap_flags & MAP_HUGETLB)) {
            cout << "Huge pages failed for buffer " << i << ", falling back to normal pages" << endl;
            mmap_flags &= ~MAP_HUGETLB;  // Remove huge pages flag
            buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, 
                                   mmap_flags, fd, buf.m.offset);
        }
        
        if (buffers[i].start == MAP_FAILED) {
            cerr << "mmap failed for V4L2 buffer " << i << ": " << strerror(errno) << endl;
            return false;
        }
        
        // Advise the kernel about our access pattern for better cache management
        // MADV_SEQUENTIAL: We'll access this buffer sequentially (good for video streaming)
        // MADV_WILLNEED: Tell kernel we'll need this soon (prefetch into cache)
        // MADV_DONTDUMP: Don't include in core dumps (optional)
        if (madvise(buffers[i].start, buffers[i].length, MADV_SEQUENTIAL) != 0) {
            perror("madvise MADV_SEQUENTIAL failed");
        }
        if (madvise(buffers[i].start, buffers[i].length, MADV_WILLNEED) != 0) {
            perror("madvise MADV_WILLNEED failed");
        }
        if (madvise(buffers[i].start, buffers[i].length, MADV_DONTDUMP) != 0) {
            perror("madvise MADV_DONTDUMP failed");
        }
        
        
        cout << "Allocated V4L2 buffer " << i << " size=" << buffers[i].length << endl;
    }
    
    return true;
}

bool V4L2Camera::startStreaming() {
    cout << "Starting streaming for device fd: " << fd << endl;
    
    // Check if device is properly opened
    if (fd == -1) {
        cerr << "Cannot start streaming - device not opened" << endl;
        return false;
    }
    
    // Queue all buffers using appropriate memory type
    v4l2_memory mem_type = use_dmabuf_import ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
    // If a PCIe bridge is attached, register the channel
    epProxySetup();

    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = mem_type;
        buf.index = i;
        
        if (use_dmabuf_import) {
            // For DMABUF import, pass the DMABUF fd and buffer length
            buf.m.fd = buffers[i].dmabuf_fd;
            buf.length = buffers[i].length;  // Must specify buffer size for DMABUF import
            buf.bytesused = 0;  // Will be filled by driver
            cout << "Queuing DMABUF buffer " << i << " with fd=" << buffers[i].dmabuf_fd 
                 << ", length=" << buffers[i].length << endl;
        }
        
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            cerr << "VIDIOC_QBUF failed for buffer " << i << " on fd " << fd 
                 << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
            return false;
        }
    }
    
    const char* mem_type_str = use_dmabuf_import ? "DMABUF import (CMA)" : "MMAP";
    cout << "Queued " << n_buffers << " buffers for NV12 capture using " << mem_type_str << " on fd " << fd << endl;
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        cerr << "VIDIOC_STREAMON failed on fd " << fd << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "This may indicate an issue with buffer setup or format configuration" << endl;
        return false;
    }
    
    cout << "V4L2 streaming started with " << mem_type_str << " buffers on fd " << fd << endl;
    return true;
}

// Method to check if allocated memory is actually contiguous
bool V4L2Camera::isMemoryContiguous() const {
    return false; // MMAP doesn't guarantee contiguous memory
}

// Get memory type as string for debugging
const char* V4L2Camera::getMemoryTypeString() const {
    return "MMAP";
}

// Check if device is properly opened and initialized
bool V4L2Camera::isDeviceReady() const {
    if (fd == -1) {
        cerr << "Device not opened (fd == -1)" << endl;
        return false;
    }
    
    if (!buffers) {
        cerr << "Buffers not allocated" << endl;
        return false;
    }
    
    if (n_buffers == 0) {
        cerr << "No buffers initialized" << endl;
        return false;
    }
    
    // Test if device is still valid by doing a simple ioctl
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        cerr << "Device fd " << fd << " is invalid: " << strerror(errno) << endl;
        return false;
    }
    
    cout << "Device fd " << fd << " is ready (buffers: " << n_buffers << ")" << endl;
    return true;
}

void V4L2Camera::close() {
    if (fd != -1) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        
        if (buffers) {
            for (unsigned int i = 0; i < n_buffers; ++i) {
                // Unmap buffer memory
                if (buffers[i].start != MAP_FAILED && buffers[i].start != nullptr) {
                    munmap(buffers[i].start, buffers[i].length);
                    buffers[i].start = nullptr;
                }
                
                // Close DMABUF file descriptor
                // For imported buffers, this is the CMA buffer fd
                // For MMAP buffers, this is the exported DMABUF fd (if exported)
                if (buffers[i].dmabuf_fd > 0) {
                    ::close(buffers[i].dmabuf_fd);
                    buffers[i].dmabuf_fd = -1;
                }
            }
            delete[] buffers;
            buffers = nullptr;
        }
        
        ::close(fd);
        fd = -1;
    }
}

void V4L2Camera::stop() {
    close();
}
