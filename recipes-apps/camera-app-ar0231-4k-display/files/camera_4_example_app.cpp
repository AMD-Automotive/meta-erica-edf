// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * camera_8_example_app.cpp
 * Example application demonstrating 8 cameras and display output using DRM.
 * Author: LT Thomas
 */

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <fstream>
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
#include <signal.h>

#include "drm_helper.h"
#include "v4l2_helper.h"

using namespace std;
using namespace std::chrono;

// Global flag for signal handling
std::atomic<bool> running(true);

// Global camera frame buffers
cv::UMat frame0(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame1(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame2(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame3(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));

bool got_frame0 = 0;
bool got_frame1 = 0;
bool got_frame2 = 0;
bool got_frame3 = 0;

// Camera capture thread function
void cameraThreadFunction(V4L2Camera* camera, cv::Mat* frame, bool* got_frame, cv::Rect tile_rect) {
    while (running) {
        *got_frame = camera->captureFrame_HD(*frame, tile_rect);
        usleep(33000);        
    }
}


// Camera capture thread function
void GstreamerFunction(cv::Mat* frame, cv::Rect tile_rect) {
    
    std::string gst_pipeline = "filesrc location=/root/video/big_buck_bunny_720p_h264.mov ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! video/x-raw,format=BGR ! appsink";

    while (running) {
        cv::VideoCapture gstreamer_cap(gst_pipeline, cv::CAP_GSTREAMER);

        if (!gstreamer_cap.isOpened()) {
            std::cerr << "Error: Could not open GStreamer pipeline" << std::endl;
            usleep(100000); // Wait 100ms before retry
            continue;
        }

        // Play until end of stream
        while (running) {
            cv::Mat gst_frame;
            if (!gstreamer_cap.read(gst_frame) || gst_frame.empty()) {
                break; // EOS - restart pipeline for loop
            }
            gst_frame.copyTo((*frame)(tile_rect));
        }
        
        gstreamer_cap.release();
    }
}


// Signal handler for Ctrl+C
void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nReceived Ctrl+C, stopping..." << std::endl;
        running = false;
    }
}

void processImage(const std::string& video0, const std::string& video1, const std::string& video2, const std::string& video3) {

    int count_main = 0; // Counter for FPS calculation
    
    DrmHelper displayoutput; // drm_helper, setups DRM buffer

    int width_in = 3840;
    int height_in = 2160;
    
    cv::Mat rgb_output = displayoutput.getDrmFramebuffer();

    std::string fps_string_camera0 = "Camera 0";
    std::string fps_string_camera1 = "Camera 1";
    std::string fps_string_camera2 = "Camera 2";
    std::string fps_string_camera3 = "Camera 3";

    V4L2Camera camera0,camera1,camera2,camera3;

    // Open camera device

    if (!camera0.open(video0.c_str())) { cerr << "Failed to open " << video0 << endl; return; }
    if (!camera0.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    if (!camera1.open(video1.c_str())) { cerr << "Failed to open " << video1 << endl; return; }
    if (!camera1.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    if (!camera2.open(video2.c_str())) { cerr << "Failed to open " << video2 << endl; return; }
    if (!camera2.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    if (!camera3.open(video3.c_str())) { cerr << "Failed to open " << video3 << endl; return; }
    if (!camera3.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }


    std::thread t0(cameraThreadFunction, &camera0, &rgb_output, &got_frame0, cv::Rect(0, 0, 1920, 1080)      );
    std::thread t1(cameraThreadFunction, &camera1, &rgb_output, &got_frame1, cv::Rect(0, 1080, 1920, 1080)    );
    std::thread t2(cameraThreadFunction, &camera2, &rgb_output, &got_frame2, cv::Rect(1920, 1080, 1920, 1080)   );
    std::thread t3(cameraThreadFunction, &camera3, &rgb_output, &got_frame3, cv::Rect(1920, 0, 1920, 1080)   );


    auto start_time = high_resolution_clock::now();

    int count0 = 0;
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;
    
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);


    
    while (running) 
    {
        auto frame_start = high_resolution_clock::now();


        // Using this as a method to avoid writing to display too fast
        if ( got_frame0 || got_frame1 || got_frame2 || got_frame3 ) { 
            if (got_frame0 && !frame0.empty()) {
                got_frame0 = 0;
                count0++;
            }
            if (got_frame1 && !frame1.empty()) {
                got_frame1 = 0;
                count1++;
            }
            if (got_frame2 && !frame2.empty()) {
                got_frame2 = 0;
                count2++;
            }
            if (got_frame3 && !frame3.empty()) {
                got_frame3 = 0;
                count3++;
            }
        }
        usleep(1000); // 1ms delay
        
        count_main++;

        if (count_main > 1000) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count0) / (elapsed_time.count() / 1000.0);
            double fps1 = static_cast<double>(count1) / (elapsed_time.count() / 1000.0);
            double fps2 = static_cast<double>(count2) / (elapsed_time.count() / 1000.0);
            double fps3 = static_cast<double>(count3) / (elapsed_time.count() / 1000.0);
            double fps_count = static_cast<double>(count_main) / (elapsed_time.count() / 1000.0);

            //Main thread execution
            //cout << "Main thread FPS: " << fps_count << " " << endl;
            
            // Create FPS string for both console output and potential use elsewhere
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(2) << "Camera 0 - FPS: " << fps;
            fps_string_camera0 = fps_stream.str();
            
            std::ostringstream fps_stream1;
            fps_stream1 << std::fixed << std::setprecision(2) << "Camera 1 - FPS: " << fps1;
            fps_string_camera1 = fps_stream1.str();

            std::ostringstream fps_stream2;
            fps_stream2 << std::fixed << std::setprecision(2) << "Camera 2 - FPS: " << fps2;
            fps_string_camera2 = fps_stream2.str();

            std::ostringstream fps_stream3;
            fps_stream3 << std::fixed << std::setprecision(2) << "Camera 3 - FPS: " << fps3;
            fps_string_camera3 = fps_stream3.str();

            cout << "cameras " << fps << " " << fps1 << " " << fps2 << " " << fps3 << " " << endl;

            count0 = 0;
            count1 = 0;
            count2 = 0;
            count3 = 0;
            count_main = 0;
            start_time = high_resolution_clock::now(); 
        }
	    
    }

    // Wait for threads to finish
    if (t0.joinable()) t0.join();
    if (t1.joinable()) t1.join();
    if (t2.joinable()) t2.join();
    if (t3.joinable()) t3.join();

    // Release camera when done
    camera0.close();
    camera1.close();
    camera2.close();
    camera3.close();
    return;
}


int main(int argc, char* argv[]) {

    // Parse command-line arguments for connector selection
    std::string connector = "CN1"; // Default to CN1
    std::string video0, video1, video2, video3;
    
    if (argc > 1) {
        connector = argv[1];
    }
    
    // Set video device paths based on connector
    if (connector == "CN1") {
        video0 = "/dev/video0";
        video1 = "/dev/video1";
        video2 = "/dev/video2";
        video3 = "/dev/video3";
        std::cout << "Using CN1 connector (video0-3)" << std::endl;
    } else if (connector == "CN2") {
        video0 = "/dev/video4";
        video1 = "/dev/video5";
        video2 = "/dev/video6";
        video3 = "/dev/video7";
        std::cout << "Using CN2 connector (video4-7)" << std::endl;
    } else if (connector == "CN3") {
        video0 = "/dev/video8";
        video1 = "/dev/video9";
        video2 = "/dev/video10";
        video3 = "/dev/video11";
        std::cout << "Using CN3 connector (video8-11)" << std::endl;
    } else {
        std::cerr << "Invalid connector specified: " << connector << std::endl;
        std::cerr << "Usage: " << argv[0] << " [CN1|CN2|CN3]" << std::endl;
        std::cerr << "  CN1: /dev/video0-3 (default)" << std::endl;
        std::cerr << "  CN2: /dev/video4-7" << std::endl;
        std::cerr << "  CN3: /dev/video8-11" << std::endl;
        return -1;
    }

    // Enable OpenCL acceleration
    if (!cv::ocl::haveOpenCL()) {
        std::cout << "OpenCL is not available. Exiting." << std::endl;
        return -1;
    }

    cv::ocl::setUseOpenCL(true);

    processImage(video0, video1, video2, video3);

    return 0;
}
