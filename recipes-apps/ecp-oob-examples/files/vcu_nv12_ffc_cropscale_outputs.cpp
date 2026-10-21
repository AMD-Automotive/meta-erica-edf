// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * vcu_nv12_ffc_cropscale_outputs.cpp
 * VCU application with NV12 format, fast frame copy, crop scaling, and multiple outputs.
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
#include "mlpp_helper.h"

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


void processImage_test() {
    int count = 0;

    // mlpp_helper
    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160, 3);

    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    MlppHelper mlpp_ffc_out1("/dev/mlpp_ffc_out1");
    MlppHelper mlpp_ffc_out2("/dev/mlpp_ffc_out2");
    MlppHelper mlpp_ffc_out3("/dev/mlpp_ffc_out3");
    MlppHelper mlpp_ffc_out4("/dev/mlpp_ffc_out4");
    MlppHelper mlpp_ffc_out5("/dev/mlpp_ffc_out5");


    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");
    // Display Crop and scale parameters
    mlpp_ffc_display.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 1, 4); // NV12
    
    // setup mlpp parameters
#if 0 // Bypass check
    mlpp_ffc_out0.update_mlpp_output_params( 1024, 256, 960, 1920+960, 792, 792+576, 255, 255, 255, 0.0, 2, 2 ); // BGRX
    mlpp_ffc_out1.update_mlpp_output_params( 640, 288, 0, 3840, 0, 1404, 255, 255, 255, 0.0, 2, 2 ); // BGRX
    mlpp_ffc_out2.update_mlpp_output_params( 1024, 448, 0, 3840, 0, 1890, 255, 255, 255, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out3.update_mlpp_output_params( 2048, 384, 0, 3840, 675, 675+810, 255, 255, 255, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out4.update_mlpp_output_params( 640, 320, 0, 3840, 907, 907+672, 255, 255, 255, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out5.update_mlpp_output_params( 480, 270, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 1, 2 ); // YUVX
#else
    mlpp_ffc_out0.update_mlpp_output_params( 1024, 256, 960, 1920+960, 792, 792+576, 0, 0, 0, 0.0, 2, 2 ); // BGRX
    mlpp_ffc_out1.update_mlpp_output_params( 640, 288, 0, 3840, 0, 1404, 0, 0, 0, 0.0, 2, 2 ); // BGRX
    mlpp_ffc_out2.update_mlpp_output_params( 1024, 448, 0, 3840, 0, 1890, 16, 16, 16, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out3.update_mlpp_output_params( 2048, 384, 0, 3840, 675, 675+810, 32, 32, 32, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out4.update_mlpp_output_params( 640, 320, 0, 3840, 907, 907+672, 64, 64, 64, 0.0, 1, 2 ); // YUVX
    mlpp_ffc_out5.update_mlpp_output_params( 480, 270, 0, 3840, 0, 2160, 128, 128, 128, 0.0, 1, 2 ); // YUVX
#endif
    /// Crop and scale parameters end
    DrmHelper drmdisplayoutput(1920, 1080, 60, DRM_OUT_NV12);
    cout << "Video Stream started " << endl;
    
    // Initialize GStreamer pipeline using OpenCV VideoCapture
    std::string gst_pipeline = "filesrc location=/root/video/bbb_sunflower_2160p_30fps_normal.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! video/x-raw,format=NV12 ! appsink emit-signals=true max-buffers=2 drop=true";
    
    
    cv::VideoCapture cap(gst_pipeline, cv::CAP_GSTREAMER);
    
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open GStreamer pipeline" << std::endl;
        return;
    }
    
    
    // Calculate the height of each color bar for horizontal bars

    cv::Mat ffc_out0_rgbx(256, 1024, CV_8UC4); // RGBX output format
    cv::Mat ffc_out1_rgbx(288, 640, CV_8UC4); // RGBX output format
    cv::Mat ffc_out2_yuvx(448, 1024, CV_8UC4); // RGBX output format
    cv::Mat ffc_out3_yuvx(384, 2048, CV_8UC4); // RGBX output format
    cv::Mat ffc_out4_yuvx(320, 640, CV_8UC4); // RGBX output format
    cv::Mat ffc_out5_yuvx(270, 480, CV_8UC4); // RGBX output format

    cv::Mat ffc_display_output(540 + 540/2, 960, CV_8UC1, cv::Scalar(0)); // NV12

    cv::Mat display_output(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0)); // NV12


    // Start non-blocking display thread
    std::thread display_thread(displayThread, &drmdisplayoutput); 
    
    auto start_time = high_resolution_clock::now();
    sleep(1);
    while (1) 
    {
        // Read frame from GStreamer pipeline (NV12 format)
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Error: Could not read frame from GStreamer pipeline" << std::endl;
            break;
        }
        
        // Write source image into MLPP pipe
        mlpp_ffc_in.write_image_to_mlpp(&frame); 

#if 0  // Serial read
        // Read the processed image from MLPP pipe 
        mlpp_ffc_out0.read_image_from_mlpp(&ffc_out0_rgbx);
        mlpp_ffc_out1.read_image_from_mlpp(&ffc_out1_rgbx);
        mlpp_ffc_out2.read_image_from_mlpp(&ffc_out2_yuvx);
        mlpp_ffc_out3.read_image_from_mlpp(&ffc_out3_yuvx);
        mlpp_ffc_out4.read_image_from_mlpp(&ffc_out4_yuvx);
        mlpp_ffc_out5.read_image_from_mlpp(&ffc_out5_yuvx);
        mlpp_ffc_display.read_image_from_mlpp(&ffc_display_output);
        drmdisplayoutput.overlay_rgbx_data_2_nv12(0, 540, ffc_out0_rgbx, display_output);
        drmdisplayoutput.overlay_rgbx_data_2_nv12(0, 540+256, ffc_out1_rgbx, display_output);
        drmdisplayoutput.overlay_yuvx_data_2_nv12(960, 0, ffc_out2_yuvx, display_output);
        //ffc_out3_yuvx to big
        drmdisplayoutput.overlay_yuvx_data_2_nv12(1024, 448, ffc_out4_yuvx, display_output);
        drmdisplayoutput.overlay_yuvx_data_2_nv12(1024, 448+320, ffc_out5_yuvx, display_output);
#else
        // Read the processed images from the MLPP pipes in parallel.
        // Each read_image_from_mlpp call blocks until its output is ready, so
        // running them on separate threads lets all outputs complete concurrently.
        std::thread read_threads[] = {
            std::thread([&]{ mlpp_ffc_out0.read_image_from_mlpp(&ffc_out0_rgbx); drmdisplayoutput.overlay_rgbx_data_2_nv12(0, 540, ffc_out0_rgbx, display_output); }),
            std::thread([&]{ mlpp_ffc_out1.read_image_from_mlpp(&ffc_out1_rgbx); drmdisplayoutput.overlay_rgbx_data_2_nv12(0, 540+256, ffc_out1_rgbx, display_output);}),
            std::thread([&]{ mlpp_ffc_out2.read_image_from_mlpp(&ffc_out2_yuvx); drmdisplayoutput.overlay_yuvx_data_2_nv12(960, 0, ffc_out2_yuvx, display_output);}),
            std::thread([&]{ mlpp_ffc_out3.read_image_from_mlpp(&ffc_out3_yuvx); }),
            std::thread([&]{ mlpp_ffc_out4.read_image_from_mlpp(&ffc_out4_yuvx); drmdisplayoutput.overlay_yuvx_data_2_nv12(1024, 448, ffc_out4_yuvx, display_output);}),
            std::thread([&]{ mlpp_ffc_out5.read_image_from_mlpp(&ffc_out5_yuvx); drmdisplayoutput.overlay_yuvx_data_2_nv12(1024, 448+320, ffc_out5_yuvx, display_output);}),
            std::thread([&]{ mlpp_ffc_display.read_image_from_mlpp(&ffc_display_output); drmdisplayoutput.overlay_nv12_data_2_nv12(0, 0, ffc_display_output, display_output);})
        };
        for (auto& t : read_threads) {
            t.join();
        }
#endif

#if 0
        // On the 50th frame, dump each output Mat to its own raw binary file.
        if (count == 50) {
            const std::pair<const char*, cv::Mat*> dumps[] = {
                {"ffc_out0_rgbx.raw", &ffc_out0_rgbx},
                {"ffc_out1_rgbx.raw", &ffc_out1_rgbx},
                {"ffc_out2_yuvx.raw", &ffc_out2_yuvx},
                {"ffc_out3_yuvx.raw", &ffc_out3_yuvx},
                {"ffc_out4_yuvx.raw", &ffc_out4_yuvx},
                {"ffc_out5_yuvx.raw", &ffc_out5_yuvx},
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


        // Queue the tiled output for display
        queueFrameForDisplay(display_output);


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
    
    //mlpp_ffc_out0.closeDevice();
    mlpp_ffc_in.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;

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
