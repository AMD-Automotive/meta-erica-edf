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
cv::UMat frame0(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame1(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame2(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame3(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame4(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame5(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame6(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
cv::UMat frame7(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));

bool got_frame0 = 0;
bool got_frame1 = 0;
bool got_frame2 = 0;
bool got_frame3 = 0;
bool got_frame4 = 0;
bool got_frame5 = 0;
bool got_frame6 = 0;
bool got_frame7 = 0;

// Camera capture thread function
void cameraThreadFunction(V4L2Camera* camera, cv::Mat* frame, bool* got_frame, cv::Rect tile_rect) {
    while (running) {
        *got_frame = camera->captureFrame(*frame, tile_rect);
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

void processImage(bool use_4cam, bool enable_video) {

    int count_main = 0; // Counter for FPS calculation
    
    DrmHelper displayoutput; // drm_helper, setups DRM buffer
    //std::thread display_thread(displayThread, &displayoutput);

    int width_in = 3840;
    int height_in = 2160;
    
    cv::Mat rgb_output = displayoutput.getDrmFramebuffer();

    std::string fps_string_camera0 = "Camera 0";
    std::string fps_string_camera1 = "Camera 1";
    std::string fps_string_camera2 = "Camera 2";
    std::string fps_string_camera3 = "Camera 3";
    std::string fps_string_camera4 = "Camera 4";
    std::string fps_string_camera5 = "Camera 5";
    std::string fps_string_camera6 = "Camera 6";
    std::string fps_string_camera7 = "Camera 7";

    V4L2Camera camera0,camera1,camera2,camera3,camera4,camera5,camera6,camera7;

    // Open camera device
    if (!camera0.open("/dev/video0")) {
        cerr << "Failed to open /dev/video0" << endl;
        return;
    }
    // Start streaming
    if (!camera0.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }

    if (!camera1.open("/dev/video1")) { cerr << "Failed to open /dev/video1" << endl; return; }
    if (!camera1.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    if (!camera2.open("/dev/video2")) { cerr << "Failed to open /dev/video2" << endl; return; }
    if (!camera2.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    if (!camera3.open("/dev/video3")) { cerr << "Failed to open /dev/video3" << endl; return; }
    if (!camera3.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }

    if (!use_4cam) {
        if (!camera4.open("/dev/video4")) { cerr << "Failed to open /dev/video4" << endl; return; }
        if (!camera4.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
        if (!camera5.open("/dev/video5")) { cerr << "Failed to open /dev/video5" << endl; return; }
        if (!camera5.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
        if (!camera6.open("/dev/video6")) { cerr << "Failed to open /dev/video6" << endl; return; }
        if (!camera6.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
        if (!camera7.open("/dev/video7")) { cerr << "Failed to open /dev/video7" << endl; return; }
        if (!camera7.startStreaming()) { cerr << "Failed to start streaming" << endl; return; }
    }



    std::thread t0(cameraThreadFunction, &camera0, &rgb_output, &got_frame0, cv::Rect(0, 0, 1280, 720)      );
    std::thread t1(cameraThreadFunction, &camera1, &rgb_output, &got_frame1, cv::Rect(0, 720, 1280, 720)    );
    std::thread t2(cameraThreadFunction, &camera2, &rgb_output, &got_frame2, cv::Rect(0, 1440, 1280, 720)   );
    std::thread t3(cameraThreadFunction, &camera3, &rgb_output, &got_frame3, cv::Rect(1280, 0, 1280, 720)   );
    
    std::thread t4, t5, t6, t7;
    if (!use_4cam) {
        t4 = std::thread(cameraThreadFunction, &camera4, &rgb_output, &got_frame4, cv::Rect(1280, 720, 1280, 720) );
        t5 = std::thread(cameraThreadFunction, &camera5, &rgb_output, &got_frame5, cv::Rect(1280, 1440, 1280, 720));
        t6 = std::thread(cameraThreadFunction, &camera6, &rgb_output, &got_frame6, cv::Rect(2560, 0, 1280, 720)   );
        t7 = std::thread(cameraThreadFunction, &camera7, &rgb_output, &got_frame7, cv::Rect(2560, 720, 1280, 720) );
    }
    

    std::thread t8;
    if (enable_video) {
        t8 = std::thread(GstreamerFunction, &rgb_output, cv::Rect(2560, 1440, 1280, 720) );
    }


    auto start_time = high_resolution_clock::now();

    int count0 = 0;
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;
    int count4 = 0;
    int count5 = 0;
    int count6 = 0;
    int count7 = 0;
    
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);


    
    while (running) 
    {
        auto frame_start = high_resolution_clock::now();


        // Using this as a method to avoid writing to display too fast
        if ( got_frame0 || got_frame1 || got_frame2 || got_frame3 || got_frame4 || got_frame5 || got_frame6 || got_frame7) { 
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

            if (got_frame4 && !frame4.empty()) {
                got_frame4 = 0;
                count4++;
            }
            if (got_frame5 && !frame5.empty()) {
                got_frame5 = 0;
                count5++;
            }
            if (got_frame6 && !frame6.empty()) {
                got_frame6 = 0;
                count6++;
            }
            if (got_frame7 && !frame7.empty()) {
                got_frame7 = 0;
                count7++;
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
            cout << "Main thread FPS: " << fps_count << " " << endl;
            
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

            std::ostringstream fps_stream4;
            fps_stream4 << std::fixed << std::setprecision(2) << "Camera 4 - FPS: " << fps3;
            fps_string_camera4 = fps_stream4.str();

            std::ostringstream fps_stream5;
            fps_stream5 << std::fixed << std::setprecision(2) << "Camera 5 - FPS: " << fps3;
            fps_string_camera5 = fps_stream5.str();

            std::ostringstream fps_stream6;
            fps_stream6 << std::fixed << std::setprecision(2) << "Camera 6 - FPS: " << fps3;
            fps_string_camera6 = fps_stream6.str();

            std::ostringstream fps_stream7;
            fps_stream7 << std::fixed << std::setprecision(2) << "Camera 7 - FPS: " << fps3;
            fps_string_camera7 = fps_stream7.str();

            //cout << "cameras " << fps << " " << fps1 << " " << fps2 << " " << fps3 << " " << endl;

            count0 = 0;
            count1 = 0;
            count2 = 0;
            count3 = 0;
            count4 = 0;
            count5 = 0;
            count6 = 0;
            count7 = 0;
            count_main = 0;
            start_time = high_resolution_clock::now(); 
        }
	    
    }

    // Wait for threads to finish
    if (t0.joinable()) t0.join();
    if (t1.joinable()) t1.join();
    if (t2.joinable()) t2.join();
    if (t3.joinable()) t3.join();
    if (!use_4cam) {
        if (t4.joinable()) t4.join();
        if (t5.joinable()) t5.join();
        if (t6.joinable()) t6.join();
        if (t7.joinable()) t7.join();
    }
    if (enable_video && t8.joinable()) t8.join();

    // Release camera when done
    camera0.close();
    camera1.close();
    camera2.close();
    camera3.close();
    if (!use_4cam) {
        camera4.close();
        camera5.close();
        camera6.close();
        camera7.close();
    }

    return;
}


int main(int argc, char* argv[]) {

    // Parse command-line arguments
    bool use_4cam = false;
    bool enable_video = true;  // Video enabled by default
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--4cam") {
            use_4cam = true;
            std::cout << "Using 4 cameras mode" << std::endl;
        } else if (arg == "--no-video") {
            enable_video = false;
            std::cout << "Video playback disabled" << std::endl;
        } else {
            std::cout << "Unknown argument: " << arg << std::endl;
            std::cout << "Usage: " << argv[0] << " [--4cam] [--no-video]" << std::endl;
            return 1;
        }
    }

    // Enable OpenCL acceleration
    if (!cv::ocl::haveOpenCL()) {
        std::cout << "OpenCL is not available. Exiting." << std::endl;
        return -1;
    }

    cv::ocl::setUseOpenCL(true);


    //Check for video file
    if (enable_video){
        const char* filename = "/root/video/big_buck_bunny_720p_h264.mov";
        std::ifstream file(filename);
        if (file.good()) {
            std::cout << "The file exists." << std::endl;
            file.close();
        } else {
            std::cout << std::endl;
            std::cout << "The video file does not exist. Download it to /root/video/:" << std::endl;
            std::cout << "wget -P /root/video/ https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_720p_h264.mov" << std::endl;
            std::cout << "sync" << std::endl;
            std::cout << "Then run this application again." << std::endl;
            std::cout << std::endl;
            return 1;
        }
    }

    processImage(use_4cam, enable_video);

    return 0;
}
