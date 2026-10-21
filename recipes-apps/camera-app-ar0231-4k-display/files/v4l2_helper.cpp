// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * v4l2_helper.cpp
 * Video4Linux2 (V4L2) helper functions for camera capture and video device management.
 * Author: LT Thomas
 */

#include "v4l2_helper.h"

using namespace std;
using namespace std::chrono;



// Constructor
V4L2Camera::V4L2Camera() : fd(-1), buffers(nullptr), n_buffers(0), width(1928), height(1208), 
                           pixel_format(0), bytes_per_line(0) {
    // Using MMAP for V4L2 buffer allocation
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
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    expbuf.index = buffer_index;
    expbuf.plane = 0;  // For single plane UYVY
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

// Capture frame and get DMABUF fd only (no OpenCV processing)
bool V4L2Camera::captureMat(cv::Mat& frame ){ //}, MlppHelper& mlpp_helper_in, MlppHelper& mlpp_helper_out) {
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
    
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1; // Single plane for UYVY
    
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
    
    // Create a Mat that wraps the MMAP buffer (UYVY format: 2 channels)
    // Use actual bytes_per_line for proper stride handling
    size_t buffer_bytes_per_line = bytes_per_line;
    cv::Mat uyvy_frame(height, buffer_bytes_per_line / 2, CV_8UC2, buffers[buf.index].start);
    
    // Convert UYVY to BGR
    cv::Mat bgr_frame;
    cv::cvtColor(uyvy_frame, bgr_frame, cv::COLOR_YUV2BGR_UYVY);
    
    // Resize to 1280x720
    cv::resize(bgr_frame, frame, cv::Size(1280, 720));
    
    // Re-queue the buffer AFTER processing is complete
    struct v4l2_buffer qbuf;
    struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(&qplanes, 0, sizeof(qplanes));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    qbuf.m.planes = qplanes;
    qbuf.length = 1;
    
    // For MMAP buffers, set the buffer length
    qplanes[0].length = buffers[buf.index].length;
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }
    
    return true;
}
#if 0
// Capture frame and get DMABUF fd only (no OpenCV processing)
bool V4L2Camera::captureFrameDmabuf(cv::Mat& frame, MlppHelper& mlpp_helper_in, MlppHelper& mlpp_helper_out) {
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
    
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1; // Single plane for UYVY
    
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
    
    // Export buffer as DMABUF
    int dmabuf_fd = getDmabufFd(buf.index);
    
    // Process the frame using DMABUF (zero-copy to ML driver)
    mlpp_helper_in.write_dmabuf_to_mlpp(dmabuf_fd, 4793344); // Use actual size of UYVY frame
    cv::Mat frame_out(540, 960, CV_8UC3);
    //usleep(3000);
    mlpp_helper_out.read_image_from_mlpp(&frame_out);
    //mlpp_helper.read_image_from_mlpp(&frame);
    
    /*std::cout << "frame_out info:" << std::endl;
	std::cout << "  Width: " << frame_out.cols << std::endl;
	std::cout << "  Height: " << frame_out.rows << std::endl;
	std::cout << "  Channels: " << frame_out.channels() << std::endl;
	std::cout << "  Type: " << frame_out.type() << std::endl;
	std::cout << "  Total elements: " << frame_out.total() << std::endl;
	std::cout << "Frame size: " << frame_out.size() << std::endl;  // Outputs: [width x height]
        */
    frame_out.copyTo(frame); // Output is RGB but wants BGR
    // Convert UYVY to BGR (RGB)
    //cv::Mat frame_out(540, 960, CV_8UC2); // UYVY: 960x540, 2 channels (U,Y)
    //mlpp_helper.read_image_from_mlpp(&frame_out);
    //cv::cvtColor(frame_out, frame, cv::COLOR_YUV2BGR_UYVY);
    
    // Re-queue the buffer AFTER processing is complete
    struct v4l2_buffer qbuf;
    struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(&qplanes, 0, sizeof(qplanes));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    qbuf.m.planes = qplanes;
    qbuf.length = 1;
    
    // For MMAP buffers, set the buffer length
    qplanes[0].length = buffers[buf.index].length;
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }
    
    return dmabuf_fd;
}
#endif 
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
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    
    // Get current format
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        cerr << "VIDIOC_G_FMT failed in setCustomStride: " << strerror(errno) << endl;
        return false;
    }
    
    // Calculate new stride with padding
    uint32_t min_bytes_per_line = fmt.fmt.pix_mp.width * 2; // UYVY = 2 bytes per pixel
    uint32_t new_stride = min_bytes_per_line + padding_bytes;
    
    cout << "Setting custom stride: " << new_stride 
         << " (width=" << fmt.fmt.pix_mp.width 
         << ", min_stride=" << min_bytes_per_line 
         << ", padding=" << padding_bytes << ")" << endl;
    
    // Set new stride
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = new_stride;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = new_stride * fmt.fmt.pix_mp.height;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        cerr << "VIDIOC_S_FMT failed setting custom stride: " << strerror(errno) << endl;
        return false;
    }
    
    // Update our internal values
    bytes_per_line = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    
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
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        cerr << "Device does not support multi-plane video capture, trying single-plane fallback" << endl;
        // Could add fallback to single-plane here if needed
        close();
        return false;
    }
    
    cout << "Device capabilities: 0x" << hex << cap.capabilities << dec << endl;
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        cerr << "Device does not support streaming" << endl;
        close();
        return false;
    }
    
    // Get default format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        cerr << "VIDIOC_G_FMT failed: " << strerror(errno) << endl;
        close();
        return false;
    }
    
    // Display current format information
    cout << "Default format: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height 
         << ", planes: " << fmt.fmt.pix_mp.num_planes
         << ", pixelformat: 0x" << hex << fmt.fmt.pix_mp.pixelformat << dec;
    
    // Convert pixel format to readable string
    char fourcc[5];
    fourcc[0] = fmt.fmt.pix_mp.pixelformat & 0xFF;
    fourcc[1] = (fmt.fmt.pix_mp.pixelformat >> 8) & 0xFF;
    fourcc[2] = (fmt.fmt.pix_mp.pixelformat >> 16) & 0xFF;
    fourcc[3] = (fmt.fmt.pix_mp.pixelformat >> 24) & 0xFF;
    fourcc[4] = '\0';
    cout << " (" << fourcc << ")" << endl;
    
    // Display plane information
    for (int i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
        cout << "Plane " << i << ": bytesperline=" << fmt.fmt.pix_mp.plane_fmt[i].bytesperline 
             << ", sizeimage=" << fmt.fmt.pix_mp.plane_fmt[i].sizeimage << endl;
    }
    
    // Update dimensions from device
    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;
    pixel_format = fmt.fmt.pix_mp.pixelformat;
    
    
    // Set progressive field type to avoid interlaced capture
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

#if 1 // Setting custom size did not work
    // OPTIONAL: Set custom bytes_per_line with extra padding
    // Calculate minimum bytes per line for UYVY (2 bytes per pixel)
    uint32_t min_bytes_per_line = width * 2;
    uint32_t desired_padding = 64; // Add 64 bytes of padding per line
    uint32_t custom_bytes_per_line = 3968;//min_bytes_per_line + desired_padding;
    
    // Try to set custom stride with padding
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = custom_bytes_per_line;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = custom_bytes_per_line * height;
    
    cout << "Requesting custom stride: " << custom_bytes_per_line 
         << " (min: " << min_bytes_per_line << ", padding: " << desired_padding << ")" << endl;
    
    // Apply the format setting with custom stride
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        cerr << "VIDIOC_S_FMT failed (setting custom stride): " << strerror(errno) << endl;
        close();
        return false;
    }
#endif

    // Check what the driver actually set
    bytes_per_line = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    
    cout << "Set progressive format: " << width << "x" << height 
         << ", planes: " << fmt.fmt.pix_mp.num_planes
         << ", format: " << fourcc 
         << ", field: " << (fmt.fmt.pix_mp.field == V4L2_FIELD_NONE ? "PROGRESSIVE" : "INTERLACED")
         << ", actual stride: " << bytes_per_line << endl;
    
   /* if (bytes_per_line != custom_bytes_per_line) {
        cout << "NOTE: Driver adjusted stride from requested " << custom_bytes_per_line 
             << " to " << bytes_per_line << endl;
    }*/
    
    return initBuffers();
}

bool V4L2Camera::initBuffers() {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 3; //4; // TODO find optimal buffer count
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    
    // Use MMAP memory type - DMA-BUF requires external buffer allocation
    // which is not implemented correctly in this code
    req.memory = V4L2_MEMORY_MMAP;
    cout << "Using MMAP memory type for V4L2 buffers" << endl;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        cerr << "VIDIOC_REQBUFS failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        return false;
    }
    
    bool using_dmabuf = false; // Always false now since we use MMAP
    cout << "Requested " << req.count << " V4L2 buffers for mplane capture using MMAP" << endl;
    
    if (req.count < 2) {
        cerr << "Insufficient buffer memory" << endl;
        return false;
    }
    
    buffers = new V4L2Buffer[req.count];
    n_buffers = req.count;
    
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1; // Single plane for UYVY
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            cerr << "VIDIOC_QUERYBUF failed: " << strerror(errno) << endl;
            return false;
        }
        
        buffers[i].length = buf.m.planes[0].length;
        buffers[i].dmabuf_fd = -1;  // Initialize DMABUF fd as invalid
        
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
        if (buf.m.planes[0].length >= (2 * 1024 * 1024)) {
            mmap_flags |= MAP_HUGETLB;
            cout << "Using huge pages for buffer " << i << " (size: " << buf.m.planes[0].length << ")" << endl;
        }
        
        buffers[i].start = mmap(NULL, buf.m.planes[0].length, PROT_READ | PROT_WRITE, 
                               mmap_flags, fd, buf.m.planes[0].m.mem_offset);
        
        // If huge pages failed, fallback to standard mapping
        if (buffers[i].start == MAP_FAILED && (mmap_flags & MAP_HUGETLB)) {
            cout << "Huge pages failed for buffer " << i << ", falling back to normal pages" << endl;
            mmap_flags &= ~MAP_HUGETLB;  // Remove huge pages flag
            buffers[i].start = mmap(NULL, buf.m.planes[0].length, PROT_READ | PROT_WRITE, 
                                   mmap_flags, fd, buf.m.planes[0].m.mem_offset);
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
    
    // Queue all buffers using MMAP memory type
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1; // Single plane for UYVY
        
        // For MMAP buffers, set the buffer length
        planes[0].length = buffers[i].length;
        
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            cerr << "VIDIOC_QBUF failed for buffer " << i << " on fd " << fd << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
            return false;
        }
    }
    
    cout << "Queued " << n_buffers << " MMAP buffers for UYVY mplane capture on fd " << fd << endl;
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        cerr << "VIDIOC_STREAMON failed on fd " << fd << ": " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "This may indicate an issue with buffer setup or format configuration" << endl;
        return false;
    }
    
    cout << "V4L2 streaming started with MMAP buffers on fd " << fd << endl;
    return true;
}

bool V4L2Camera::captureFrame(cv::Mat& frame, cv::Rect TileRect ) {
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
    
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1; // Single plane for UYVY
    
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            return false; // No frame available
        }
        cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "Device fd: " << fd << ", buffers: " << (buffers ? "valid" : "null") << ", n_buffers: " << n_buffers << endl;
        return false;
    }

    // For 4ppc mode, check if we need to adjust the stride
    // The diagonal issue often occurs when the stride doesn't match the expected width
    size_t buffer_bytes_per_line = bytes_per_line;
    size_t expected_bytes_per_line = width * 2; // UYVY = 2 bytes per pixel
    
    //Stride mismatch detected - bytesperline: 3872, expected: 3856
    
 #if 0 // opencv  
    // Create OpenCV Mat with proper stride handling using MMAP buffer
    // Use the actual bytes per line from the buffer instead of calculated width
    cv::Mat uyvy_frame(height, buffer_bytes_per_line / 2, CV_8UC2, buffers[buf.index].start);
    //Scaling is done here
    cv::Mat frame_out(720, 1280, CV_8UC3);
    // Crop to 1920x1208 (centered crop from original frame)
    cv::Mat uyvy_cropped = uyvy_frame(cv::Rect(0, 0, 1920, 1208));
    // Convert UYVY to BGR
    cv::Mat bgr_frame;
    cv::cvtColor(uyvy_cropped, bgr_frame, cv::COLOR_YUV2BGR_UYVY);
    // Resize to 1280x720
    cv::resize(bgr_frame, frame, cv::Size(1280, 720));

#else // OpenCL
    cv::Mat uyvy_frame(height, buffer_bytes_per_line / 2, CV_8UC2, buffers[buf.index].start);

    cv::UMat umat_uyvy_frame;
    uyvy_frame.copyTo(umat_uyvy_frame);

    //Scaling is done here
    cv::UMat frame_out(720, 1280, CV_8UC3);
    // Crop to 1920x1208 (centered crop from original frame)
    cv::UMat uyvy_cropped = umat_uyvy_frame(cv::Rect(0, 0, 1920, 1208));
    // Convert UYVY to BGR
    cv::UMat bgr_frame;
    cv::cvtColor(uyvy_cropped, bgr_frame, cv::COLOR_YUV2BGR_UYVY);
    // Resize to 1280x720
    cv::resize(bgr_frame, frame_out, cv::Size(1280, 720));
    frame_out.copyTo(frame(TileRect));

#endif

    // Re-queue the MMAP buffer
    struct v4l2_buffer qbuf;
    struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(&qplanes, 0, sizeof(qplanes));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    qbuf.m.planes = qplanes;
    qbuf.length = 1;
    
    // For MMAP buffers, set the buffer length
    qplanes[0].length = buffers[buf.index].length;
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }
    
    return true;
}

bool V4L2Camera::captureFrame_HD(cv::Mat& frame, cv::Rect TileRect ) {
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
    
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1; // Single plane for UYVY
    
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            return false; // No frame available
        }
        cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << " (errno: " << errno << ")" << endl;
        cerr << "Device fd: " << fd << ", buffers: " << (buffers ? "valid" : "null") << ", n_buffers: " << n_buffers << endl;
        return false;
    }

    // For 4ppc mode, check if we need to adjust the stride
    // The diagonal issue often occurs when the stride doesn't match the expected width
    size_t buffer_bytes_per_line = bytes_per_line;
    size_t expected_bytes_per_line = width * 2; // UYVY = 2 bytes per pixel
    
    //Stride mismatch detected - bytesperline: 3872, expected: 3856
    
    cv::Mat uyvy_frame(height, buffer_bytes_per_line / 2, CV_8UC2, buffers[buf.index].start);

    cv::UMat umat_uyvy_frame;
    uyvy_frame.copyTo(umat_uyvy_frame);

    //Scaling is done here
    cv::UMat frame_out(1080, 1920, CV_8UC3);
    // Crop to 1920x1208 (centered crop from original frame)
    cv::UMat uyvy_cropped = umat_uyvy_frame(cv::Rect(0, 0, 1920, 1208));
    // Convert UYVY to BGR
    cv::UMat bgr_frame;
    cv::cvtColor(uyvy_cropped, bgr_frame, cv::COLOR_YUV2BGR_UYVY);
    // Resize to 1280x720
    cv::resize(bgr_frame, frame_out, cv::Size(1920, 1080));
    frame_out.copyTo(frame(TileRect));

    // Re-queue the MMAP buffer
    struct v4l2_buffer qbuf;
    struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(&qplanes, 0, sizeof(qplanes));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buf.index;
    qbuf.m.planes = qplanes;
    qbuf.length = 1;
    
    // For MMAP buffers, set the buffer length
    qplanes[0].length = buffers[buf.index].length;
    
    if (ioctl(fd, VIDIOC_QBUF, &qbuf) == -1) {
        cerr << "VIDIOC_QBUF failed: " << strerror(errno) << endl;
        return false;
    }
    
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
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        
        if (buffers) {
            for (unsigned int i = 0; i < n_buffers; ++i) {
                // Close DMABUF file descriptor if it was exported
                if (buffers[i].dmabuf_fd > 0) {
                    ::close(buffers[i].dmabuf_fd);
                    buffers[i].dmabuf_fd = -1;
                }
                
                // Unmap MMAP buffer
                if (buffers[i].start != MAP_FAILED && buffers[i].start != nullptr) {
                    munmap(buffers[i].start, buffers[i].length);
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
