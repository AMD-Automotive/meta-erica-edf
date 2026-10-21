// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * vcu_opencv_4k_nv12in_4k_nv12_drm.cpp
 * VCU application for NV12 format video processing with DRM display output.
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
#include <fstream> 

#include "drm_helper.h"


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
            displayoutput->DrmWriteOutputNV12VSync(&local_frame);
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


void processImage_test() {
    int count = 0;

    int scalex, scaley, width_in, height_in;
    scalex=1920;
    scaley=1080;
    width_in=3840;
    height_in=2160;

    /// Crop and scale parameters end
    DrmHelper drmdisplayoutput(3840, 2160, 30, DRM_OUT_NV12); // drm_helper, setups DRM buffer
    cout << "Video Stream started " << endl;
    
    // Initialize GStreamer pipeline using OpenCV VideoCapture
    std::string gst_pipeline = "filesrc location=/root/video/bbb_sunflower_2160p_30fps_normal.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! video/x-raw,format=NV12 ! appsink emit-signals=true max-buffers=2 drop=true";
    
    
    cv::VideoCapture cap(gst_pipeline, cv::CAP_GSTREAMER);
    
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open GStreamer pipeline" << std::endl;
        return;
    }
    
    // Create the final tiled_output by concatenating Y, U, V planes
    cv::Mat tiled_output(height_in * 3, width_in, CV_8UC1);
    
    int width = width_in;
    int height = height_in;


    // Start non-blocking display thread
    std::thread display_thread(displayThread, &drmdisplayoutput); 
    
    auto start_time = high_resolution_clock::now();
    //sleep(20);
    while (1) 
    {
        // Read frame from GStreamer pipeline (NV12 format)
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Error: Could not read frame from GStreamer pipeline" << std::endl;
            break;
        }
        
        // Queue the tiled output for display
        queueFrameForDisplay(frame);


        if (count > 99) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            cout << std::fixed << std::setprecision(2) << "FPS: " << fps << endl;
            count = 0;
            start_time = high_resolution_clock::now();
        
        }
        count++;
	    
    }
    
    // Stop display thread and cleanup
    display_running = false;
    display_thread.join();
  
    
    // Close VideoCapture
    cap.release();
    std::cout << "GStreamer pipeline closed." << std::endl;

    return;
}


int main() {

    //Check for file
    const char* filename = "/root/video/bbb_sunflower_2160p_30fps_normal.mp4";
    std::ifstream file(filename);
    if (file.good()) {
        std::cout << "The file exists." << std::endl;
        file.close(); // Remember to close the file
    } else {
        std::cout << std::endl;
        std::cout << "The video file does not exist. Download it to /root/video/:" << std::endl;
        std::cout << "wget -P /root/video/ https://download.blender.org/demo/movies/BBB/bbb_sunflower_2160p_30fps_normal.mp4.zip" << std::endl;
        std::cout << "unzip /root/video/bbb_sunflower_2160p_30fps_normal.mp4.zip -d /root/video/" << std::endl;
        std::cout << "sync" << std::endl;
        std::cout << "rm /root/video/bbb_sunflower_2160p_30fps_normal.mp4.zip" << std::endl;
        std::cout << "Then run this application again." << std::endl;
        std::cout << std::endl;
        return 1;
    }   
    
    processImage_test();

    return 0;
}
