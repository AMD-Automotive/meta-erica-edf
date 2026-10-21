// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * single_imx623.cpp
 * Example camera capture application taking NV12 from ISP of 5 cameras.
 *  4x IMX623 on sensor ID's 0,1,2,3 and IMX728 on sensor ID 8
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
#include "mlpp_helper.h"
#include "isp_helper.h"

//typedef unsigned short u16; 

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

void processImage_test(const char* device_path) {
    int count = 0;
    int count_main = 0; // Counter for FPS calculation

    // FFC is still needed to control MLPP pipeline
    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160);


    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");
    mlpp_ffc_display.update_mlpp_output_params( 1920, 1080, 0, 3840, 0, 2160,  255, 255, 255, 0.0, 1, 4); // NV12


    /// Crop and scale parameters end
    DrmHelper displayoutput(1920, 1080, 60, DRM_OUT_NV12);
    cout << "Video Stream started " << endl;

    sleep(1);

    // Start non-blocking display thread
    std::thread display_thread(displayThread, &displayoutput);

    cout << "Camera streaming started " << endl;

    std::string fps_string_camera0 = "Camera 0";
    std::string fps_string_camera1 = "Camera 1";
    std::string fps_string_camera2 = "Camera 2";
    std::string fps_string_camera3 = "Camera 3";
    std::string fps_string_camera4 = "Camera 4";

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
    
    cout << "Camera0 opened successfully using " << camera0.getMemoryTypeString() << endl;

    cv::Mat tiled_output(1080, 1920, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat resized_frame0(540, 960, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat nv12_display_output(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0));
    
    auto start_time = high_resolution_clock::now();
    sleep(1);   
    while(1)
    {

        // Capture frame using V4L2Camera with DMABUF support
        bool got_frame0 = camera0.captureFrameDmabuf( mlpp_ffc_in );
        
        if (got_frame0) {
            mlpp_ffc_display.read_image_from_mlpp(&nv12_display_output);
            // Add label for Camera 0
            cv::putText(nv12_display_output, fps_string_camera0, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
            
            // Queue the tiled output for display
            queueFrameForDisplay(nv12_display_output);
            count++;
        }
            

        //usleep(5000); // 1ms delay


        count_main++;

        if (count_main > 100) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            
            // Create FPS string for both console output and potential use elsewhere
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(2) << device_path << " - FPS: " << fps;
            fps_string_camera0 = fps_stream.str();
            
            
            cout << "camera " << device_path << ": " << fps << endl;

            count = 0;
            count_main = 0;
            start_time = high_resolution_clock::now(); 
        }
	   
    }
    
    
    // Stop display thread and cleanup
    display_running = false;
    
    display_thread.join();
    //mlpp_pipe.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;

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

    processImage_test(device_path);

    return 0;
}
