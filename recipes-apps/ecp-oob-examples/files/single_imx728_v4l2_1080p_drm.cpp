// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * single_imx728_dmabuf_1080p_drm.cpp
 * Example camera capture application taking NV12 from ISP and sending NV12 output to DRM (RGB 1080P).
 * Uses V4L2 for the capture
 *  
 * Author: LT Thomas
 */

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
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

#include "drm_helper.h"
#include "isp_helper.h"

using namespace std;
using namespace std::chrono;

// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;

// Non-blocking display thread function
void displayThread(DrmHelper* displayoutput) {
    cv::Mat local_frame;
    
    while (display_running) {
        bool has_frame = false;
        
        // Check for new frame
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (new_frame_available) {
                display_frame.copyTo(local_frame);
                new_frame_available = false;
                has_frame = true;
            }
        }
        
        // Display frame if available
        if (has_frame && !local_frame.empty()) {
            displayoutput->DrmWriteOutputVSync(&local_frame);
        }
        
        // Small delay to prevent excessive CPU usage
        usleep(1000); // 1ms
    }
}

// Non-blocking function to queue frame for display
void queueFrameForDisplay(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.copyTo(display_frame);
    new_frame_available = true;
}

void processImage_test(const char* device_path) {
    int count = 0;
    int count_main = 0; // Counter for FPS calculation


    /// Crop and scale parameters end
    DrmHelper displayoutput; // drm_helper, setups DRM buffer
    cout << "Video Stream started " << endl;

    sleep(1);

    // Start non-blocking display thread
    std::thread display_thread(displayThread, &displayoutput);

    cout << "Camera streaming started " << endl;

    std::string fps_string_camera = " ";

    // Use V4L2Camera for direct DMABUF access instead of OpenCV VideoCapture
    V4L2Camera camera0;
    
    // Open camera device with DMABUF support
    if (!camera0.open(device_path, 3840, 2160)) {
        cerr << "Failed to open " << device_path << endl;
        return;
    }
    
    // Start streaming
    if (!camera0.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }

    // Let the driver queue 1 extra frame before the first paced read so there
    camera0.setTargetPeriodMs(33.3333333); // ~30 fps grid    
    // is a buffering cushion (adds ~1 frame of latency). Set 0 to disable.
    camera0.setPacingPrimeFrames(1); 

    cout << "Camera0 opened successfully using " << camera0.getMemoryTypeString() << endl;

    cv::Mat frame_nv12;
    cv::Mat frame_bgr_full;
    cv::Mat frame_bgr_1080;
    cv::Mat frame0(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
    struct timeval ts;
    
    auto start_time = high_resolution_clock::now();
    sleep(1);   
    while(1)
    {

        // Capture NV12 frame using V4L2Camera
        bool got_frame0 = camera0.captureFrameDmabufNV12(frame_nv12, &ts, false);
        
        if (got_frame0) {
            // Convert NV12 (3840x2160) to BGR and resize to 1920x1080 for display.
            cv::cvtColor(frame_nv12, frame_bgr_full, cv::COLOR_YUV2BGR_NV12);
            cv::resize(frame_bgr_full, frame_bgr_1080, cv::Size(1920, 1080), 0, 0,
                       cv::INTER_LINEAR);
            frame_bgr_1080.copyTo(frame0);

            // Add label for Camera 0
            cv::putText(frame0, fps_string_camera, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
            
            // Queue the tiled output for display
            queueFrameForDisplay(frame0);
            count++;
        }
            
        count_main++;

        if (count_main > 100) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            
            // Create FPS string for both console output and potential use elsewhere
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(2) << device_path << " - FPS: " << fps;
            fps_string_camera = fps_stream.str();
            
            
            cout << "camera " << device_path << ": " << fps << endl;

            count = 0;
            count_main = 0;
            start_time = high_resolution_clock::now(); 
        }
	   
    }
    
    
    // Stop display thread and cleanup
    display_running = false;
    
    display_thread.join();
    std::cout << "Capture device closed." << std::endl;

    // Stop all cameras
    camera0.stop();
    
    cout << "All cameras stopped" << endl;

    return;
}


int main(int argc, char* argv[]) {
    const char* device_path = "/dev/video10";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d") {
            if (i + 1 >= argc) {
                cerr << "Usage: " << argv[0] << " [-d /dev/videoX]" << endl;
                return 1;
            }
            device_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            cout << "Usage: " << argv[0] << " [-d /dev/videoX]" << endl;
            cout << "Default device: /dev/video10" << endl;
            return 0;
        } else {
            cerr << "Unknown option: " << arg << endl;
            cerr << "Usage: " << argv[0] << " [-d /dev/videoX]" << endl;
            return 1;
        }
    }

    cout << "Opening " << device_path << endl;

    //processImage();
    processImage_test(device_path);

    return 0;
}
