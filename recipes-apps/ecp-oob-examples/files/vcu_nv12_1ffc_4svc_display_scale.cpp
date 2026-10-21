// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * vcu_nv12_display_scale.cpp
 * VCU application for NV12 format video processing with display scaling capabilities.
 * Author: LT Thomas
 */


#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <csignal>
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


// Non-blocking function to queue frame for display
void queueFrameForDisplay(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.copyTo(display_frame);
    new_frame_available = true;
}


std::atomic<bool> ctrlc_pressed{false};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        ctrlc_pressed = true;
    }
}

void processImage_test() {
    int count = 0;
    int frame_num = 0;

    // mlpp_helper
    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160 );

    MlppHelper mlpp_svc0_in("/dev/mlpp_svc0_in");
    MlppHelper mlpp_svc1_in("/dev/mlpp_svc1_in");
    MlppHelper mlpp_svc2_in("/dev/mlpp_svc2_in");
    MlppHelper mlpp_svc3_in("/dev/mlpp_svc3_in");
    int expected_y_value = 1536;
    mlpp_svc0_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc1_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc2_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc3_in.update_mlpp_input_params( 1920, 1536, 3);


    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");
    MlppHelper mlpp_svc0_display("/dev/mlpp_svc0_display");
    MlppHelper mlpp_svc1_display("/dev/mlpp_svc1_display");
    MlppHelper mlpp_svc2_display("/dev/mlpp_svc2_display");
    MlppHelper mlpp_svc3_display("/dev/mlpp_svc3_display");
    mlpp_svc0_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc1_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc2_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc3_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);


    int scalex, scaley, width_in, height_in;
    scalex=960;
    scaley=540;
    width_in=3840;
    height_in=2160;

    // Display Crop and scale parameters
    mlpp_ffc_display.update_mlpp_output_params( scalex, scaley, 0, width_in, 0, height_in, 0, 0, 0, 0.0, 0, 0 );



    /// Crop and scale parameters end
    DrmHelper drmdisplayoutput; // drm_helper, setups DRM buffer
    cout << "Video Stream started " << endl;
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    
    // Initialize GStreamer pipeline using OpenCV VideoCapture
    std::string gst_pipeline = "filesrc location=/root/video/bbb_sunflower_2160p_30fps_normal.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! video/x-raw,format=NV12 ! appsink emit-signals=true max-buffers=2 drop=true";
    std::string gst_pipeline_svc0 = "filesrc location=/root/video/KOR_SVC1_h264_transcode.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! videorate ! video/x-raw,format=NV12,framerate=15/1 ! appsink emit-signals=true max-buffers=2 drop=true";
    std::string gst_pipeline_svc1 = "filesrc location=/root/video/KOR_SVC2_h264_transcode.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! videorate ! video/x-raw,format=NV12,framerate=15/1 ! appsink emit-signals=true max-buffers=2 drop=true";
    std::string gst_pipeline_svc2 = "filesrc location=/root/video/KOR_SVC3_h264_transcode.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! videorate ! video/x-raw,format=NV12,framerate=15/1 ! appsink emit-signals=true max-buffers=2 drop=true";
    std::string gst_pipeline_svc3 = "filesrc location=/root/video/KOR_SVC4_h264_transcode.mp4 ! qtdemux ! h264parse ! omxh264dec low-latency=0 ! videorate ! video/x-raw,format=NV12,framerate=15/1 ! appsink emit-signals=true max-buffers=2 drop=true";
    

    cv::VideoCapture cap(gst_pipeline, cv::CAP_GSTREAMER);
    cv::VideoCapture cap_svc0(gst_pipeline_svc0, cv::CAP_GSTREAMER);
    cv::VideoCapture cap_svc1(gst_pipeline_svc1, cv::CAP_GSTREAMER);
    cv::VideoCapture cap_svc2(gst_pipeline_svc2, cv::CAP_GSTREAMER);
    cv::VideoCapture cap_svc3(gst_pipeline_svc3, cv::CAP_GSTREAMER);

    
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open GStreamer pipeline" << std::endl;
        return;
    }
    
    // Create the final tiled_output by concatenating Y, U, V planes
    cv::Mat tiled_output(height_in * 3, width_in, CV_8UC1);
    
    int width = width_in;
    int height = height_in;
    // Calculate the height of each color bar for horizontal bars

    //cv::Mat vpss_output_yuv444(scaley*3, scalex, CV_8UC1); // YUV444 output format
    cv::Mat rgb_output(1080, 1920, CV_8UC3, cv::Scalar(128, 128, 128));

    cv::Mat ffc_output(540, 960, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat svc0_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat svc1_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat svc2_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat svc3_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    

    
    auto start_time = high_resolution_clock::now();
    //sleep(20);

    bool running = true;
    while (running) 
    {
        // Check for Ctrl+C
        if (ctrlc_pressed) {
            std::cerr << "Ctrl+C detected, cleaning up..." << std::endl;
            break;
        }
        // Read frame from GStreamer pipeline (NV12 format)
        cv::Mat frame;
        cv::Mat framesvc0;
        cv::Mat framesvc1;
        cv::Mat framesvc2;
        cv::Mat framesvc3;
        if (!cap.read(frame)) {
            std::cerr << "End of File: cap" << std::endl;
            break;
        }
        bool svc_frames_available = false;
        if (frame_num % 2) // Run SVC at 15fps
        {
            bool svc0_ok = cap_svc0.read(framesvc0);
            bool svc1_ok = cap_svc1.read(framesvc1);
            bool svc2_ok = cap_svc2.read(framesvc2);
            bool svc3_ok = cap_svc3.read(framesvc3);
            
            if (!svc0_ok || !svc1_ok || !svc2_ok || !svc3_ok) {
                std::cerr << "One or more SVC streams ended, continuing with main stream only" << std::endl;
                svc_frames_available = false;
            } else {
                svc_frames_available = true;
            }
        }

        // Write SVC first
        // Write SVC in parallel because memcopy slow
        if (frame_num % 2 && svc_frames_available) {
            std::thread t_svc0([&](){ mlpp_svc0_in.write_image_to_mlpp(&framesvc0); });
            std::thread t_svc1([&](){ mlpp_svc1_in.write_image_to_mlpp(&framesvc1); });
            std::thread t_svc2([&](){ mlpp_svc2_in.write_image_to_mlpp(&framesvc2); });
            std::thread t_svc3([&](){ mlpp_svc3_in.write_image_to_mlpp(&framesvc3); });
            t_svc0.join();
            t_svc1.join();
            t_svc2.join();
            t_svc3.join();
        }

        // Write FFC image last which kicks off MLPP pipe
        mlpp_ffc_in.write_image_to_mlpp(&frame); 

        // Read the processed image from MLPP pipe in order
        // Read the processed images from MLPP pipes in parallel
        if (frame_num % 2 && svc_frames_available) {
            std::thread t_ffc_disp([&](){ mlpp_ffc_display.read_image_from_mlpp(&ffc_output); });
            std::thread t_svc0_disp([&](){ mlpp_svc0_display.read_image_from_mlpp(&svc0_output); });
            std::thread t_svc1_disp([&](){ mlpp_svc1_display.read_image_from_mlpp(&svc1_output); });
            std::thread t_svc2_disp([&](){ mlpp_svc2_display.read_image_from_mlpp(&svc2_output); });
            std::thread t_svc3_disp([&](){ mlpp_svc3_display.read_image_from_mlpp(&svc3_output); });
            t_ffc_disp.join();
            t_svc0_disp.join();
            t_svc1_disp.join();
            t_svc2_disp.join();
            t_svc3_disp.join();
        }
        else{
            mlpp_ffc_display.read_image_from_mlpp(&ffc_output); 
        }

        // Copy ffc_output (960x540) into the top-left of rgb_output (1920x1080)
        cv::Rect roi(0, 0, ffc_output.cols, ffc_output.rows);
        ffc_output.copyTo(rgb_output(roi));

        // Copy svc0_output, svc1_output, svc2_output, svc3_output into rgb_output
        // svc0_output: top-right (960,0)
        cv::Rect roi_svc0(960, 0, svc0_output.cols, svc0_output.rows);
        svc0_output.copyTo(rgb_output(roi_svc0));

        // svc1_output: bottom-left (0,540)
        cv::Rect roi_svc1(0, 540, svc1_output.cols, svc1_output.rows);
        svc1_output.copyTo(rgb_output(roi_svc1));

        // svc2_output: bottom-center (640,540)
        cv::Rect roi_svc2(640, 540, svc2_output.cols, svc2_output.rows);
        svc2_output.copyTo(rgb_output(roi_svc2));

        // svc3_output: bottom-right (1280,540)
        cv::Rect roi_svc3(1280, 540, svc3_output.cols, svc3_output.rows);
        svc3_output.copyTo(rgb_output(roi_svc3));

        drmdisplayoutput.DrmWriteOutputFast(&rgb_output);

        frame_num++;
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

    // Cleanup section
    cap.release();
    cap_svc0.release();
    cap_svc1.release();
    cap_svc2.release();
    cap_svc3.release();
    std::cout << "GStreamer pipeline closed." << std::endl;

    mlpp_ffc_display.closeDevice();
    mlpp_ffc_in.closeDevice();
    mlpp_svc0_display.closeDevice();
    mlpp_svc1_display.closeDevice();
    mlpp_svc2_display.closeDevice();
    mlpp_svc3_display.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;

    return;
}


int main() {

    //Check for file
    const char* filename = "/root/video/KOR_SVC1_h264_transcode.mp4";
    std::ifstream file(filename);
    if (file.good()) {
        std::cout << "The file exists." << std::endl;
        file.close(); // Remember to close the file
    } else {
        std::cout << std::endl;
        std::cout << "The video file does not exist. Download it to /root/video/:" << std::endl;
        std::cout << "Check dropbox for all video files" << std::endl;
        std::cout << std::endl;
        return 1;
    }   
    
    processImage_test();

    return 0;
}
