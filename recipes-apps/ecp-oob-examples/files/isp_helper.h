// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * v4l2_helper.h
 * Header file for Video4Linux2 (V4L2) helper functions and camera capture operations.
 * Author: LT Thomas
 */

#ifndef V4L2_HELPER_H
#define V4L2_HELPER_H

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <stdio.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <string.h>
#include <cstdint> 
#include <fstream>

#include "mlpp_helper.h"


// Fallback definition if dma-heap.h is not available
#ifndef DMA_HEAP_IOCTL_ALLOC
#define DMA_HEAP_IOCTL_ALLOC    _IOWR('H', 0x0, struct dma_heap_allocation_data)

struct dma_heap_allocation_data {
    __u64 len;
    __u32 fd;
    __u32 fd_flags;
    __u64 heap_flags;
};
#endif


#include "drm_helper.h"
#include "mlpp_helper.h"
#include "ep_proxy_bridge.h"

using namespace std;
using namespace std::chrono;

// V4L2 buffer structure for both MMAP and DMABUF import
struct V4L2Buffer {
    void* start;        // Mapped CPU address (for MMAP or mmap'd DMABUF)
    size_t length;      // Buffer size
    int dmabuf_fd;      // DMABUF file descriptor
    bool is_imported;   // True if buffer was pre-allocated and imported
};
    

// V4L2 camera class
class V4L2Camera {
private:
    int fd;
    V4L2Buffer* buffers;
    unsigned int n_buffers;
    int width, height;
    uint32_t pixel_format;
    uint32_t bytes_per_line;
    bool use_dmabuf_import;  // True to use CMA buffers with DMABUF import

    // Software pacing state: when enabled, captureFrameDmabuf() holds its return
    // until the next deadline so it returns on a fixed period grid.
    std::chrono::steady_clock::time_point next_return_deadline;
    std::chrono::nanoseconds frame_period{0};  // 0 = pacing target not set
    bool pacing_started = false;
    int  pacing_prime_frames = 0;   // extra frames to let the driver queue before the first paced read
    bool pacing_primed = false;     // true once the one-time warm-up has run

    // Optional PCIe frame-sharing bridge. When set, each captured frame is
    // also published over PCIe (BRAM mailbox + MSI-X) on channel ep_channel_.
    EpProxyBridge* ep_bridge_ = nullptr;
    int  ep_channel_ = -1;
    bool ep_setup_done_ = false;    // channel registered + phys addrs discovered

    // Register the channel and discover buffer physical addresses with the
    // bridge. Safe to call multiple times; runs its work only once, and must
    // run before VIDIOC_STREAMON (while buffers hold no real frame data).
    void epProxySetup();

    // Helper to allocate CMA buffer via DMA-BUF heap
    int allocateCmaBuffer(size_t size);
    
    // Helper to initialize buffers with DMABUF import
    bool initBuffersDmabufImport();

public:
    // Constructor
    V4L2Camera();
    
    // Destructor
    ~V4L2Camera();
    
    // Helper function to verify memory contiguity
    bool verifyMemoryContiguity(int buffer_index);
    
    // Export buffer as DMABUF for hardware device sharing
    int exportBufferAsDmabuf(int buffer_index);
    
    // Get DMABUF fd for a specific buffer (export if not already done)
    int getDmabufFd(int buffer_index);
    
    bool captureFrameDmabufNV12(cv::Mat& frame_nv12, struct timeval* out_timestamp = nullptr, bool pace = false);

    // Set the target return period (in milliseconds) used when pace == true.
    // A value <= 0 disables pacing.
    void setTargetPeriodMs(double ms);

    // When pacing, let the driver capture and queue this many extra frames
    // before the first paced read, building a buffering cushion that smooths
    // out jitter (at the cost of that many frames of latency). The achievable
    // cushion is bounded by the number of queued V4L2 buffers. 0 disables it.
    void setPacingPrimeFrames(int frames);
    // Attach a PCIe frame-sharing bridge and the mailbox channel index this
    // camera should publish on. Call after open() and before startStreaming().
    // Pass bridge == nullptr to disable sharing.
    void setEpProxy(EpProxyBridge* bridge, int channel);

    // Capture frame and get DMABUF fd only (no OpenCV processing)
    bool captureFrameDmabuf( MlppHelper& mlpp_helper_in );
    
    // Helper function to check for MMAP memory support
    bool checkMmapMemorySupport();
    
    // Helper function to set custom stride padding
    bool setCustomStride(uint32_t padding_bytes);
    
    // Device operations
    bool open(const char* device);
    bool open(const char* device, int desired_width, int desired_height);
    bool initBuffers();
    bool startStreaming();
    void close();
    void stop();
    
    // Method to check if allocated memory is actually contiguous
    bool isMemoryContiguous() const;
    
    // Get memory type as string for debugging
    const char* getMemoryTypeString() const;
    
    // Check if device is properly opened and initialized
    bool isDeviceReady() const;
    
    // Getters
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

#endif // V4L2_HELPER_H
