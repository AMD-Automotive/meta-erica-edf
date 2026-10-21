// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * mlpp_color_scale.cpp
 * Example application demonstrating MLPP preprocessing with scaled color outputs and display output using DRM.
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
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160); //NV12

    MlppHelper mlpp_ffc_display;
    mlpp_ffc_display.openDevice("/dev/mlpp_ffc_display");
    mlpp_ffc_display.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 255, 255, 255 , 1.0, 1, 4 ); // make YUV output and scale to 960x540

    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    MlppHelper mlpp_ffc_out1("/dev/mlpp_ffc_out1");
    MlppHelper mlpp_ffc_out2("/dev/mlpp_ffc_out2");
    MlppHelper mlpp_ffc_out3("/dev/mlpp_ffc_out3");
    MlppHelper mlpp_ffc_out4("/dev/mlpp_ffc_out4");
    MlppHelper mlpp_ffc_out5("/dev/mlpp_ffc_out5");
    mlpp_ffc_out0.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, .25, 1, 2 ); // YUVX
    mlpp_ffc_out1.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, .50, 1, 2 ); // YUVX
    mlpp_ffc_out2.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, 1.0, 1, 2 ); // YUVX
    mlpp_ffc_out3.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, 2.0, 1, 2 ); // YUVX
    mlpp_ffc_out4.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, 4.0, 1, 2 ); // YUVX
    mlpp_ffc_out5.update_mlpp_output_params( 960, 270, 0, 3840, 0, 1404, 255, 255, 255, 8.0, 1, 2 ); // YUVX

    DrmHelper displayoutput(1920, 1080, 60, DRM_OUT_NV12);

    int width_in = 3840;
    int height_in = 2160;
    cv::Mat nv12_output_mat(height_in + height_in / 2, width_in, CV_8UC1, cv::Scalar(100));
    generateColorBarsNV12(nv12_output_mat, width_in, height_in);
    
    
    if (nv12_output_mat.isContinuous()) {
        std::cout << "Mat is continuous. Writing entire data block in one go." << std::endl;
    } 

    cv::Mat nv12_display_output(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0)); // NV12


    cv::Mat mlpp_display(540 + 540/2, 960, CV_8UC1, cv::Scalar(0));

    cv::Mat mlpp_ffc0(270, 960, CV_8UC4, cv::Scalar(0));
    cv::Mat mlpp_ffc1(270, 960, CV_8UC4, cv::Scalar(0));
    cv::Mat mlpp_ffc2(270, 960, CV_8UC4, cv::Scalar(0));
    cv::Mat mlpp_ffc3(270, 960, CV_8UC4, cv::Scalar(0));
    cv::Mat mlpp_ffc4(270, 960, CV_8UC4, cv::Scalar(0));
    cv::Mat mlpp_ffc5(270, 960, CV_8UC4, cv::Scalar(0));
    
    cv::Mat mlpp_ffc0_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    cv::Mat mlpp_ffc1_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    cv::Mat mlpp_ffc2_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    cv::Mat mlpp_ffc3_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    cv::Mat mlpp_ffc4_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    cv::Mat mlpp_ffc5_nv12(270 + 270/2, 960, CV_8UC1, cv::Scalar(0));
    

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


        // Read NV12 output from MLPP pipe when it is ready, this call blocks until the output is ready
        mlpp_ffc_display.read_image_from_mlpp(&mlpp_display);
        mlpp_ffc_out0.read_image_from_mlpp(&mlpp_ffc0);                
        mlpp_ffc_out1.read_image_from_mlpp(&mlpp_ffc1);
        mlpp_ffc_out2.read_image_from_mlpp(&mlpp_ffc2);
        mlpp_ffc_out3.read_image_from_mlpp(&mlpp_ffc3);
        mlpp_ffc_out4.read_image_from_mlpp(&mlpp_ffc4);
        mlpp_ffc_out5.read_image_from_mlpp(&mlpp_ffc5);

        cv::putText(mlpp_ffc0, "0.5", cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);
        cv::putText(mlpp_ffc1, "0.25",cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);
        cv::putText(mlpp_ffc2, "1.0", cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);
        cv::putText(mlpp_ffc3, "2.0", cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);
        cv::putText(mlpp_ffc4, "4.0", cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);
        cv::putText(mlpp_ffc5, "8.0", cv::Point(100, 100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);

        displayoutput.overlay_nv12_data_2_nv12(0, 0, mlpp_display, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(0, 540, mlpp_ffc0, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(0, 540+270, mlpp_ffc1, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(960, 0, mlpp_ffc2, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(960, 270, mlpp_ffc3, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(960, 270+270, mlpp_ffc4, nv12_display_output);
        displayoutput.overlay_yuvx_data_2_nv12(960, 270+270+270, mlpp_ffc5, nv12_display_output);

        // Write it to display
        displayoutput.DrmWriteOutputNV12VSync(&nv12_display_output);

        if (count > 99) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            cout << std::fixed << std::setprecision(2) << "Full Pipe (i.e. 7 outputs) FPS: " << fps << endl;
            count = 0;
            start_time = high_resolution_clock::now();        
        }
        count++;
	    
    }
    // Cleanup
    std::cout << "ML preprocessing device closed." << std::endl;
    
    return;
}


int main() {

    processImage();

    return 0;
}
