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

#include "drm_helper.h"

using namespace std;
using namespace std::chrono;

// V4L2 buffer structure for MMAP buffers
struct V4L2Buffer {
    void* start;
    size_t length;
    int dmabuf_fd;  // DMABUF file descriptor for export
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

    bool captureMat(cv::Mat& frame);
    
    // Helper function to check for MMAP memory support
    bool checkMmapMemorySupport();
    
    // Helper function to set custom stride padding
    bool setCustomStride(uint32_t padding_bytes);
    
    // Device operations
    bool open(const char* device);
    bool initBuffers();
    bool startStreaming();
    bool captureFrame(cv::Mat& frame, cv::Rect );
    bool captureFrame_HD(cv::Mat& frame, cv::Rect );
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
