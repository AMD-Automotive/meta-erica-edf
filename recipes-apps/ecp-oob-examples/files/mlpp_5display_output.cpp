// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * mlpp_5display_output.cpp
 * Example application demonstrating MLPP preprocessing and display output using DRM.
 * Example shows use of all Inputs with surround view inputs being varied
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
// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;

#define BGR_RGB_CHECK 0

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
    mlpp_ffc_display.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 1, 4); // NV12

#if BGR_RGB_CHECK
    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    MlppHelper mlpp_ffc_out1("/dev/mlpp_ffc_out1");
    mlpp_ffc_out0.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 2, 2 ); // BGRX
    mlpp_ffc_out1.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 0, 2 ); // RGBX
#endif

    MlppHelper mlpp_svc0_in("/dev/mlpp_svc0_in");
    MlppHelper mlpp_svc1_in("/dev/mlpp_svc1_in");
    MlppHelper mlpp_svc2_in("/dev/mlpp_svc2_in");
    MlppHelper mlpp_svc3_in("/dev/mlpp_svc3_in");
    
    //DMA BUF SIZE actually ends up being 2048x1536 instead of 1920x1536
    mlpp_svc0_in.update_mlpp_input_params( 2048, 1536);
    mlpp_svc1_in.update_mlpp_input_params( 2048, 1536);
    mlpp_svc2_in.update_mlpp_input_params( 2048, 1536);
    mlpp_svc3_in.update_mlpp_input_params( 2048, 1536);

    MlppHelper mlpp_svc0_display("/dev/mlpp_svc0_display");
    MlppHelper mlpp_svc1_display("/dev/mlpp_svc1_display");
    MlppHelper mlpp_svc2_display("/dev/mlpp_svc2_display");
    MlppHelper mlpp_svc3_display("/dev/mlpp_svc3_display");
    mlpp_svc0_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4); // NV12
    mlpp_svc1_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);
    mlpp_svc2_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);
    mlpp_svc3_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);


    DrmHelper drmdisplayoutput(1920, 1080, 60, DRM_OUT_NV12);
    // Start non-blocking display thread
    std::thread display_thread(displayThread, &drmdisplayoutput);

    int width_in = 3840;
    int height_in = 2160;
    cv::Mat nv12_output_mat(height_in + height_in / 2, width_in, CV_8UC1, cv::Scalar(0));
    generateColorBarsNV12(nv12_output_mat, width_in, height_in);

    int width_in_3mp = 2048;
    int height_in_3mp = 1536;
    cv::Mat nv12_output_mat_3mp(height_in_3mp + height_in_3mp / 2, width_in_3mp, CV_8UC1, cv::Scalar(0));
    generateColorBarsNV12(nv12_output_mat_3mp, width_in_3mp, height_in_3mp);

    
    
    
    if (nv12_output_mat.isContinuous()) {
        std::cout << "Mat is continuous. Writing entire data block in one go." << std::endl;
    } 

    cv::Mat display_output(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0)); // NV12

    int average_mlpp_time = 0;
    auto start_time = high_resolution_clock::now();
    
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);

    cv::Mat ffc_output(540 + 540/2, 960, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc0_output(512 + 512/2, 640, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc1_output(512 + 512/2, 640, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc2_output(512 + 512/2, 640, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc3_output(512 + 512/2, 640, CV_8UC1, cv::Scalar(0)); // NV12

    cv::Mat mlpp_ffc0(540, 960, CV_8UC4, cv::Scalar(128, 128, 128)); // BGRX
    cv::Mat mlpp_ffc1(540, 960, CV_8UC4, cv::Scalar(128, 128, 128)); // RGBX


    while (running) 
    {
        auto frame_start = high_resolution_clock::now();

        //Make an output that has a text counting on it
        cv::Mat nv12_output_w_counter = nv12_output_mat.clone();
        std::string customText = std::to_string(count);
        cv::putText(nv12_output_w_counter, customText, cv::Point(100, 200), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);

        cv::Mat nv12_output_3mp_w_counter = nv12_output_mat_3mp.clone();
        cv::putText(nv12_output_3mp_w_counter, customText, cv::Point(100, 200), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);

        int new_svc_frame[4] = {0,0,0,0};


        // Write all inputs in parallel; each write_image_to_mlpp blocks until
        // its pipe accepts the frame, so running them on separate threads lets
        // the writes proceed concurrently.
        std::thread write_threads[] = {
            std::thread([&]{ mlpp_svc0_in.write_image_to_mlpp(&nv12_output_3mp_w_counter); new_svc_frame[0] = 1; }),
            std::thread([&]{ mlpp_svc1_in.write_image_to_mlpp(&nv12_output_3mp_w_counter); new_svc_frame[1] = 1; }),
            std::thread([&]{ mlpp_svc2_in.write_image_to_mlpp(&nv12_output_3mp_w_counter); new_svc_frame[2] = 1; }),
            std::thread([&]{ mlpp_svc3_in.write_image_to_mlpp(&nv12_output_3mp_w_counter); new_svc_frame[3] = 1; }),
            // Write NV12 image into MLPP pipe
            // If the pipeline is still running then it will block the start of the next cycle
            std::thread([&]{ mlpp_ffc_in.write_image_to_mlpp(&nv12_output_w_counter); })
        };
        for (auto& t : write_threads) {
            t.join();
        }

        // Read every display output and overlay it into display_output in
        // parallel. Each read_image_from_mlpp blocks until its output is ready,
        // and the overlays target non-overlapping regions of display_output so
        // they are safe to run concurrently.
        std::thread read_threads[] = {
            std::thread([&]{
                mlpp_ffc_display.read_image_from_mlpp(&ffc_output);
                drmdisplayoutput.overlay_nv12_data_2_nv12(0, 0, ffc_output, display_output);
            }),
            std::thread([&]{
                if (new_svc_frame[0]) {
                    mlpp_svc0_display.read_image_from_mlpp(&svc0_output);
                    drmdisplayoutput.overlay_nv12_data_2_nv12(1024, 0, svc0_output, display_output);
                }
            }),
            std::thread([&]{
                if (new_svc_frame[1]) {
                    mlpp_svc1_display.read_image_from_mlpp(&svc1_output);
                    drmdisplayoutput.overlay_nv12_data_2_nv12(0, 540, svc1_output, display_output);
                }
            }),
            std::thread([&]{
                if (new_svc_frame[2]) {
                    mlpp_svc2_display.read_image_from_mlpp(&svc2_output);
                    drmdisplayoutput.overlay_nv12_data_2_nv12(640, 540, svc2_output, display_output);
                }
            }),
            std::thread([&]{
                if (new_svc_frame[3]) {
                    mlpp_svc3_display.read_image_from_mlpp(&svc3_output);
                    drmdisplayoutput.overlay_nv12_data_2_nv12(1280, 540, svc3_output, display_output);
                }
            })
        };
        for (auto& t : read_threads) {
            t.join();
        }

#if BGR_RGB_CHECK
        mlpp_ffc_out0.read_image_from_mlpp(&mlpp_ffc0);
        mlpp_ffc_out1.read_image_from_mlpp(&mlpp_ffc1);

        // On count 10, dump the ffc0/ffc1 Mats to raw binary files.
        if (count == 10) {
            const std::pair<const char*, cv::Mat*> dumps[] = {
                {"mlpp_ffc0.raw", &mlpp_ffc0},
                {"mlpp_ffc1.raw", &mlpp_ffc1},
            };
            for (const auto& d : dumps) {
                cv::Mat m = d.second->isContinuous() ? *d.second : d.second->clone();
                std::ofstream ofs(d.first, std::ios::binary);
                if (ofs) {
                    ofs.write(reinterpret_cast<const char*>(m.data),
                              static_cast<std::streamsize>(m.total() * m.elemSize()));
                    cout << "Wrote " << d.first << endl;
                } else {
                    cerr << "Failed to open " << d.first << " for writing" << endl;
                }
            }
        }
#endif
        // Write it to display
        queueFrameForDisplay(display_output);

        if (count > 99) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            cout << std::fixed << std::setprecision(2) << "Display Pipe (i.e. 5 outputs) FPS: " << fps << endl;
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
