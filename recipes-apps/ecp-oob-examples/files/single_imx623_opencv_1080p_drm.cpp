// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * single_imx623_opencv_1080p_drm.cpp
 * Example camera capture application taking NV12 from ISP of IMX623 camera
 * Uses OpenCV for the capture
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
#include <vector>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "drm_helper.h"
#include "isp_helper.h"


using namespace std;
using namespace std::chrono;

// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;
int camera_id;

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

void processImage_test(int device_index, bool enable_display) {
    int count = 0;
    int count_main = 0; // Counter for FPS calculation

    /// Crop and scale parameters end
    // DRM display is optional; constructed only when display is enabled.
    std::unique_ptr<DrmHelper> displayoutput;
    std::thread display_thread;
    if (enable_display) {
        displayoutput.reset(new DrmHelper()); // drm_helper, setups DRM buffer
        cout << "Video Stream started " << endl;
        sleep(1);
        // Start non-blocking display thread
        display_thread = std::thread(displayThread, displayoutput.get());
    } else {
        cout << "Video Stream started (display disabled)" << endl;
        sleep(1);
    }

    cout << "Camera streaming started " << endl;

    std::string fps_string_camera = "Camera X";

    // Open camera using OpenCV VideoCapture
    cv::VideoCapture camera0;
    
    // Open video device based on provided device index
    if (!camera0.open(device_index, cv::CAP_V4L2)) {
        cerr << "Failed to open /dev/video" << device_index << " with OpenCV" << endl;
        return;
    }
    
    // CRITICAL: Disable automatic RGB conversion to get raw NV12 data
    camera0.set(cv::CAP_PROP_CONVERT_RGB, 0);
    
    // Set format to NV12
    camera0.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('N', 'V', '1', '2'));
    
    // Set camera resolution to 1936x1536
    camera0.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
    camera0.set(cv::CAP_PROP_FRAME_HEIGHT, 1536);
    
    // Verify settings
    int actual_width = camera0.get(cv::CAP_PROP_FRAME_WIDTH);
    int actual_height = camera0.get(cv::CAP_PROP_FRAME_HEIGHT);
    int fourcc = camera0.get(cv::CAP_PROP_FOURCC);
    
    char fourcc_str[5];
    fourcc_str[0] = fourcc & 0xFF;
    fourcc_str[1] = (fourcc >> 8) & 0xFF;
    fourcc_str[2] = (fourcc >> 16) & 0xFF;
    fourcc_str[3] = (fourcc >> 24) & 0xFF;
    fourcc_str[4] = '\0';
    
    cout << "Camera opened at " << actual_width << "x" << actual_height 
         << ", format: " << fourcc_str << endl;
    
    if (actual_width != 1920 || actual_height != 1536) {
        cerr << "WARNING: Could not set desired resolution. Got " << actual_width << "x" << actual_height << endl;
    }

    cv::Mat tiled_output(1080, 1920, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat resized_frame0(540, 960, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat frame0(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat captured_frame;

    // Inter-frame timing stats (microseconds between consecutive successful frames)
    std::vector<double> frame_intervals_us;
    frame_intervals_us.reserve(1024);
    bool have_last_frame_time = false;
    auto last_frame_time = high_resolution_clock::now();

    auto start_time = high_resolution_clock::now();
    sleep(1);   
    while(1)
    {

        // Read frame from OpenCV VideoCapture
        bool got_frame0 = camera0.read(captured_frame);
        
        if (got_frame0 && !captured_frame.empty()) {

            // Record time between consecutive successful frames
            auto now_frame_time = high_resolution_clock::now();
            if (have_last_frame_time) {
                double dt_us = duration_cast<microseconds>(
                    now_frame_time - last_frame_time).count();
                frame_intervals_us.push_back(dt_us);
            }
            last_frame_time = now_frame_time;
            have_last_frame_time = true;

            // Convert captured NV12 (1920x1536) to BGR, then resize to 1920x1080.
            // With CAP_PROP_CONVERT_RGB=0, OpenCV returns raw NV12 bytes. The buffer
            // layout is Y plane (HxW) followed by interleaved UV plane (H/2 x W),
            // giving a total height of H * 3/2. Wrap as a single-channel Mat for cvtColor.
            const int src_w = 1920;
            const int src_h = 1536;
            cv::Mat nv12(src_h * 3 / 2, src_w, CV_8UC1, captured_frame.data);
            cv::Mat bgr_full;
            cv::cvtColor(nv12, bgr_full, cv::COLOR_YUV2BGR_NV12);
            cv::resize(bgr_full, frame0, cv::Size(1920, 1080), 0, 0, cv::INTER_LINEAR);

            // Add label for Camera 0
            cv::putText(frame0, fps_string_camera, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
            
            // Queue the tiled output for display (only when display is enabled)
            if (enable_display) {
                queueFrameForDisplay(frame0);
            }
            count++;
        }


        count_main++;

        if (count_main > 100) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            
            // Create FPS string for both console output and potential use elsewhere
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(2) << "Camera " << camera_id << " - FPS: " << fps;
            fps_string_camera = fps_stream.str();

            // Compute inter-frame interval stats over this reporting window
            double dt_min_ms = 0.0, dt_max_ms = 0.0, dt_std_ms = 0.0, dt_mean_ms = 0.0;
            size_t n = frame_intervals_us.size();
            if (n > 0) {
                double mn = std::numeric_limits<double>::infinity();
                double mx = -std::numeric_limits<double>::infinity();
                double sum = 0.0;
                for (double v : frame_intervals_us) {
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                    sum += v;
                }
                double mean = sum / static_cast<double>(n);
                double sq_sum = 0.0;
                for (double v : frame_intervals_us) {
                    double d = v - mean;
                    sq_sum += d * d;
                }
                double variance = (n > 1) ? (sq_sum / static_cast<double>(n - 1)) : 0.0;
                dt_min_ms  = mn   / 1000.0;
                dt_max_ms  = mx   / 1000.0;
                dt_mean_ms = mean / 1000.0;
                dt_std_ms  = std::sqrt(variance) / 1000.0;
            }

            cout << std::fixed << std::setprecision(2)
                 << "camera " << fps
                 << " | dt(ms) n=" << n
                 << " min="  << dt_min_ms
                 << " max="  << dt_max_ms
                 << " mean=" << dt_mean_ms
                 << " std="  << dt_std_ms
                 << endl;

            frame_intervals_us.clear();
            count = 0;
            count_main = 0;
            start_time = high_resolution_clock::now(); 
        }
	   
    }
    
    
    // Stop display thread and cleanup
    display_running = false;

    if (display_thread.joinable()) {
        display_thread.join();
    }
    
    cout << "All cameras stopped" << endl;

    return;
}


int main(int argc, char* argv[]) {
    // Parse optional flags and positional camera_id.
    bool enable_display = true;
    int positional_count = 0;
    const char* camera_id_arg = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-display") {
            enable_display = false;
        } else if (a == "-h" || a == "--help") {
            positional_count = -1; // force usage print
            break;
        } else if (!a.empty() && a[0] == '-') {
            cerr << "Unknown option: " << a << endl;
            positional_count = -1;
            break;
        } else {
            if (positional_count == 0) camera_id_arg = argv[i];
            positional_count++;
        }
    }

    if (positional_count != 1 || camera_id_arg == nullptr) {
        cerr << "Usage: " << argv[0] << " [--no-display] <sensor_id>" << endl;
        cerr << "  --no-display : skip DRM display output (capture/stats only)" << endl;
        cerr << "  sensor_id    : 0..11" << endl;
        cerr << "    0 -> /dev/video0 IMX623(0)" << endl;
        cerr << "    1 -> /dev/video2 IMX623(1)" << endl;
        cerr << "    2 -> /dev/video4 IMX623(2)" << endl;
        cerr << "    3 -> /dev/video6 IMX623(3)" << endl;
        cerr << "    4 -> /dev/video10 IMX623(4)" << endl;
        cerr << "    5 -> /dev/video12 IMX623(5)" << endl;
        cerr << "    6 -> /dev/video14 IMX623(6)" << endl;
        cerr << "    7 -> /dev/video16 IMX623(7)" << endl;
        cerr << "    8 -> /dev/video20 IMX623(8)" << endl;
        cerr << "    9 -> /dev/video22 IMX623(9)" << endl;
        cerr << "   10 -> /dev/video24 IMX623(10)" << endl;
        cerr << "   11 -> /dev/video26 IMX623(11)" << endl;
        return 1;
    }

    // Parse camera ID argument
    camera_id = atoi(camera_id_arg);
    
    // Validate camera ID
    if (camera_id < 0 || camera_id > 11) {
        cerr << "Error: camera_id must be in range 0..11" << endl;
        return 1;
    }

    // Map camera ID to video device index
    // 0 -> 0, 1 -> 2, 2 -> 4, 3 -> 6, 4 -> 10, 5 -> 12, 6 -> 14, 7 -> 16,
    // 8 -> 20, 9 -> 22, 10 -> 24, 11 -> 26
    int device_index;
    if (camera_id < 4) {
        device_index = camera_id * 2;
    } else if (camera_id < 8) {
        device_index = camera_id * 2 + 2;
    } else {
        device_index = camera_id * 2 + 4;
    }
    
    cout << "Opening /dev/video" << device_index << " (sensor_id: " << camera_id
         << ", display: " << (enable_display ? "on" : "off") << ")" << endl;

    //processImage();
    processImage_test(device_index, enable_display);

    return 0;
}
