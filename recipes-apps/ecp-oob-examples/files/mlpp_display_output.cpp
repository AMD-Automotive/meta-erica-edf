// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * mlpp_display_output.cpp
 * Example application demonstrating MLPP preprocessing and display output using DRM.
 * Author: LT Thomas
 */

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
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
#include "mlpp_helper.h"
#include "colorbar_helper.h"

using namespace std;
using namespace std::chrono;

// Global flag for signal handling
std::atomic<bool> running(true);

// Signal handler for Ctrl+C
void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nReceived Ctrl+C, stopping..." << std::endl;
        running = false;
    }
}

void processImage() {
    int count = 0;
    
    // mlpp_helper
    MlppHelper mlpp_ffc_in; 
    mlpp_ffc_in.openDevice("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160);

    MlppHelper mlpp_ffc_display;
    mlpp_ffc_display.openDevice("/dev/mlpp_ffc_display");
    int width_out = 1920;
    int height_out= 1080;
    mlpp_ffc_display.update_mlpp_output_params(width_out, height_out, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 1, 4);
    
    
    //DrmHelper displayoutput; // drm_helper, setups DRM buffer
    DrmHelper displayoutput(1920, 1080, 60, DRM_OUT_NV12);

    int width_in = 3840;
    int height_in = 2160;
    cv::Mat nv12_output_mat(height_in + height_in / 2, width_in, CV_8UC1, cv::Scalar(0));
    generateColorBarsNV12(nv12_output_mat, width_in, height_in);
    
    
    if (nv12_output_mat.isContinuous()) {
        std::cout << "Mat is continuous. Writing entire data block in one go." << std::endl;
    } 

    
    //cv::Mat rgb_output(1080, 1920, CV_8UC3, cv::Scalar(128, 128, 128));

    cv::Mat nv12_display_output(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0));
    cv::Mat nv12_fvc_display_output(height_out + height_out/2, width_out, CV_8UC1, cv::Scalar(0));



    int average_mlpp_time = 0;
    auto start_time = high_resolution_clock::now();
    
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);
    
    while (running) 
    {
        auto frame_start = high_resolution_clock::now();

        //Make an output that has a text counting on it
        cv::Mat nv12_output_w_counter = nv12_output_mat.clone();
        std::string customText = std::to_string(count);
        cv::putText(nv12_output_w_counter, customText, cv::Point(100, 200), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);

        // Write NV12 image into MLPP pipe
        // If the pipeline is still running then it will block the start of the next cycle
        mlpp_ffc_in.write_image_to_mlpp(&nv12_output_w_counter);

        // HW pipe FBRD -> CSC -> Crop/Scale -> FBWR

        // Read RGB output from MLPP pipe when it is ready, this call blocks until the output is ready
        mlpp_ffc_display.read_image_from_mlpp(&nv12_fvc_display_output);

        // Copy the FVC display output into the top-left corner of the display output
        nv12_fvc_display_output.copyTo(nv12_display_output(cv::Rect(0, 0, nv12_fvc_display_output.cols, nv12_fvc_display_output.rows)));


        // Write it to display
        displayoutput.DrmWriteOutputNV12VSync(&nv12_display_output);

        // Control frame rate to 30 FPS (33.33ms per frame)
        auto frame_end = high_resolution_clock::now();
        auto frame_duration = duration_cast<microseconds>(frame_end - frame_start);
        auto target_duration = microseconds(33333); // 1/30 second = 33333 microseconds
        

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
    // Cleanup
    mlpp_ffc_display.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;
    
    return;
}


int main() {

    processImage();

    return 0;
}
