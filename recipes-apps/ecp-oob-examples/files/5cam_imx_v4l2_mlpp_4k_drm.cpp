// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 *
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

#include "drm_helper.h"
#include "mlpp_helper.h"
#include "isp_helper.h"
#include "ep_proxy_bridge.h"

using namespace std;
using namespace std::chrono;

// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;
bool got_frame0, got_frame1, got_frame2, got_frame3, got_frame4 = false;

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

void processImage_test(const char* device_path, bool enable_ep_proxy) {
    int count,count1,count2,count3,count4 = 0;
    int count_main = 0; // Counter for FPS calculation


    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160, 3);

    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");
    mlpp_ffc_display.update_mlpp_output_params( 2560, 1080, 0, 3840, 0, 2160,  255, 255, 255, 1.0, 1, 4); // NV12

    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    mlpp_ffc_out0.update_mlpp_output_params( 512, 512, 0, 3840, 0, 2160,  255, 255, 255, 1.0, 0, 2); // RGBX

    MlppHelper mlpp_svc0_in("/dev/mlpp_svc0_in");
    MlppHelper mlpp_svc1_in("/dev/mlpp_svc1_in");
    MlppHelper mlpp_svc2_in("/dev/mlpp_svc2_in");
    MlppHelper mlpp_svc3_in("/dev/mlpp_svc3_in");
    
    //DMA BUF SIZE actually ends up being 2048x1536 instead of 1920x1536
    mlpp_svc0_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc1_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc2_in.update_mlpp_input_params( 1920, 1536, 3);
    mlpp_svc3_in.update_mlpp_input_params( 1920, 1536, 3);

    MlppHelper mlpp_svc0_display("/dev/mlpp_svc0_display");
    MlppHelper mlpp_svc1_display("/dev/mlpp_svc1_display");
    MlppHelper mlpp_svc2_display("/dev/mlpp_svc2_display");
    MlppHelper mlpp_svc3_display("/dev/mlpp_svc3_display");
    mlpp_svc0_display.update_mlpp_output_params(1280, 1080, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4); // NV12
    mlpp_svc1_display.update_mlpp_output_params(1280, 1080, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);
    mlpp_svc2_display.update_mlpp_output_params(1280, 1080, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);
    mlpp_svc3_display.update_mlpp_output_params(1280, 1080, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 1, 4);

    MlppHelper mlpp_svc0_out("/dev/mlpp_svc0_out");
    MlppHelper mlpp_svc1_out("/dev/mlpp_svc1_out");
    MlppHelper mlpp_svc2_out("/dev/mlpp_svc2_out");
    MlppHelper mlpp_svc3_out("/dev/mlpp_svc3_out");
    mlpp_svc0_out.update_mlpp_output_params(512, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 0, 2); //RGBX
    mlpp_svc1_out.update_mlpp_output_params(512, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 0, 2); //RGBX
    mlpp_svc2_out.update_mlpp_output_params(512, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 0, 2); //RGBX
    mlpp_svc3_out.update_mlpp_output_params(512, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 0, 2); //RGBX


    /// Crop and scale parameters end
    DrmHelper displayoutput(3840, 2160, 30, DRM_OUT_NV12);
    cout << "Video Stream started " << endl;

    sleep(1);

    // Start non-blocking display thread
    std::thread display_thread(displayThread, &displayoutput);

    cout << "Camera streaming started " << endl;

    // PCIe frame-sharing bridge (MDB5 endpoint). Publishes each captured frame
    // to the host over PCIe (BRAM mailbox + MSI-X) in addition to local use.
    // Only initialized when --ep_proxy is requested.
    EpProxyBridge ep_bridge;
    bool ep_ok = false;
    if (enable_ep_proxy) {
        ep_ok = ep_bridge.init(5 /* channels */);
        if (!ep_ok) {
            cout << "PCIe frame sharing disabled (bridge init failed); local-only." << endl;
        }
    } else {
        cout << "PCIe frame sharing disabled (--ep_proxy not set); local-only." << endl;
    }

    std::string fps_string_camera0 = "Camera 0";

    // Use V4L2Camera for direct DMABUF access instead of OpenCV VideoCapture
    V4L2Camera camera0,camera1,camera2,camera3,camera4;

    // Open camera device with DMABUF support
    if (!camera0.open(device_path, 3840, 2160)) {
        cerr << "Failed to open " << device_path << endl;
        return;
    }

    // Share this camera on mailbox channel 4 (the 4K /dev/video10 stream)
    if (ep_ok) camera0.setEpProxy(&ep_bridge, 4);

    // Start streaming
    if (!camera0.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    cout << "Camera0 opened successfully using " << camera0.getMemoryTypeString() << endl;

    
    // Open camera device
    if (!camera4.open("/dev/video0", 1920, 1536)) {
        cerr << "Failed to open /dev/video0" << endl;
        return;
    }
    if (ep_ok) camera4.setEpProxy(&ep_bridge, 0);
    // Start streaming
    if (!camera4.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    cout << "Camera4 opened successfully using " << camera4.getMemoryTypeString() << endl;

    // Open camera device
    if (!camera1.open("/dev/video2", 1920, 1536)) {
        cerr << "Failed to open /dev/video2" << endl;
        return;
    }
    if (ep_ok) camera1.setEpProxy(&ep_bridge, 1);
    // Start streaming
    if (!camera1.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    cout << "Camera1 opened successfully" << endl;

    // Open camera device
    if (!camera2.open("/dev/video4", 1920, 1536)) {
        cerr << "Failed to open /dev/video4" << endl;
        return;
    }
    if (ep_ok) camera2.setEpProxy(&ep_bridge, 2);
    // Start streaming
    if (!camera2.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    cout << "Camera2 opened successfully" << endl;

    // Open camera device
    if (!camera3.open("/dev/video6", 1920, 1536)) {
        cerr << "Failed to open /dev/video6" << endl;
        return;
    }
    if (ep_ok) camera3.setEpProxy(&ep_bridge, 3);
    // Start streaming
    if (!camera3.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    cout << "Camera3 opened successfully" << endl;

    camera0.setTargetPeriodMs(33.3333333);
    camera0.setPacingPrimeFrames(1); 
    camera1.setTargetPeriodMs(33.3333333);
    camera1.setPacingPrimeFrames(1); 
    camera2.setTargetPeriodMs(33.3333333);
    camera2.setPacingPrimeFrames(1); 
    camera3.setTargetPeriodMs(33.3333333);
    camera3.setPacingPrimeFrames(1); 
    camera4.setTargetPeriodMs(33.3333333);
    camera4.setPacingPrimeFrames(1); 


    // All channels registered and phys addresses discovered: publish the
    // mailbox header magic so the PCIe host can begin frame discovery.
    if (ep_ok) ep_bridge.finalize();


    cv::Mat nv12_display_output(2160 + 2160/2, 3840, CV_8UC1, cv::Scalar(0));
    cv::Mat nv12_ffc_output(1080 + 1080/2, 2560, CV_8UC1, cv::Scalar(0));
    cv::Mat ffc0_output(512, 512, CV_8UC4);
    cv::Mat svc0_output(512, 512, CV_8UC4);
    cv::Mat svc1_output(512, 512, CV_8UC4);
    cv::Mat svc2_output(512, 512, CV_8UC4);
    cv::Mat svc3_output(512, 512, CV_8UC4);

    cv::Mat svc0_display(1080 + 1080/2, 1280, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc1_display(1080 + 1080/2, 1280, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc2_display(1080 + 1080/2, 1280, CV_8UC1, cv::Scalar(0)); // NV12
    cv::Mat svc3_display(1080 + 1080/2, 1280, CV_8UC1, cv::Scalar(0)); // NV12



    // Run each camera's capture + MLPP read completely independently in its own
    // persistent thread, decoupled from the main while(1) loop below. The main
    // loop just composites whatever the latest processed frames are and pushes
    // them to the display.
    std::atomic<bool> capture_running{true};
    std::thread capture_threads[] = {
        std::thread([&]{ while (capture_running) { got_frame0 = camera0.captureFrameDmabuf(mlpp_ffc_in);mlpp_ffc_display.read_image_from_mlpp(&nv12_ffc_output); displayoutput.overlay_nv12_data_2_nv12(0, 0, nv12_ffc_output, nv12_display_output);    mlpp_ffc_out0.read_image_from_mlpp(&ffc0_output); } }),
        std::thread([&]{ while (capture_running) { got_frame1 = camera1.captureFrameDmabuf(mlpp_svc1_in); mlpp_svc1_display.read_image_from_mlpp(&svc1_display); displayoutput.overlay_nv12_data_2_nv12(2560, 0, svc1_display, nv12_display_output);    mlpp_svc1_out.read_image_from_mlpp(&svc1_output);} }),
        std::thread([&]{ while (capture_running) { got_frame2 = camera2.captureFrameDmabuf(mlpp_svc2_in); mlpp_svc2_display.read_image_from_mlpp(&svc2_display); displayoutput.overlay_nv12_data_2_nv12(1280, 1080, svc2_display, nv12_display_output); mlpp_svc2_out.read_image_from_mlpp(&svc2_output);} }),
        std::thread([&]{ while (capture_running) { got_frame3 = camera3.captureFrameDmabuf(mlpp_svc3_in); mlpp_svc3_display.read_image_from_mlpp(&svc3_display); displayoutput.overlay_nv12_data_2_nv12(2560, 1080, svc3_display, nv12_display_output); mlpp_svc3_out.read_image_from_mlpp(&svc3_output);} }),
        std::thread([&]{ while (capture_running) { got_frame4 = camera4.captureFrameDmabuf(mlpp_svc0_in); mlpp_svc0_display.read_image_from_mlpp(&svc0_display); displayoutput.overlay_nv12_data_2_nv12(0, 1080, svc0_display, nv12_display_output);    mlpp_svc0_out.read_image_from_mlpp(&svc0_output);} })
    };

    auto start_time = high_resolution_clock::now();
    sleep(1);
    while(1)
    {
 
        if ( got_frame0 || got_frame1 || got_frame2 || got_frame3 || got_frame4 )
            queueFrameForDisplay(nv12_display_output);

        if (got_frame0) 
        {
            got_frame0 = false;
            count++;
        }
        if (got_frame1) 
        {
            got_frame1 = false;
            count1++;
        }
        if (got_frame2) 
        {
            got_frame2 = false;
            count2++;
        }
        if (got_frame3) 
        {
            got_frame3 = false;
            count3++;
        }
        if (got_frame4) 
        {
            got_frame4 = false;
            count4++;
        }


        // Queue the output for display
        usleep(1000); // 5ms



        count_main++;

        auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
        if (elapsed_time.count() >= 2000) // Calculate FPS once every 2 seconds
        {
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            double fps1 = static_cast<double>(count1) / (elapsed_time.count() / 1000.0);
            double fps2 = static_cast<double>(count2) / (elapsed_time.count() / 1000.0);
            double fps3 = static_cast<double>(count3) / (elapsed_time.count() / 1000.0);
            double fps4 = static_cast<double>(count4) / (elapsed_time.count() / 1000.0);

            // Create FPS string for both console output and potential use elsewhere
            std::ostringstream fps_stream;
            fps_stream << std::fixed << std::setprecision(2) << device_path << " - FPS: " << fps;
            //fps_stream << std::fixed << std::setprecision(2) << " FPS: " << fps << " " << fps1 << " " << fps2 << " " << fps3 << " " << fps4 << " ";
            fps_string_camera0 = fps_stream.str();


            cout << std::fixed << std::setprecision(2) << " FPS: " << fps << " " << fps1 << " " << fps2 << " " << fps3 << " " << fps4 << endl;;

            count = count1 = count2 = count3 = count4 = 0;
            count_main = 0;
            start_time = high_resolution_clock::now();
        }

    }


    // Stop capture threads
    capture_running = false;
    for (auto& t : capture_threads) {
        if (t.joinable()) t.join();
    }

    // Stop display thread and cleanup
    display_running = false;

    display_thread.join();
    std::cout << "ML preprocessing device closed." << std::endl;

    // Stop all cameras
    camera0.stop();

    cout << "All cameras stopped" << endl;

    return;
}


int main(int argc, char* argv[]) {
    const char* device_path = "/dev/video10"; // 4K stream (camera0) -> mailbox ch4
    bool enable_ep_proxy = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d") {
            if (i + 1 >= argc) {
                cerr << "Usage: " << argv[0] << " [-d /dev/videoX] [--ep_proxy]" << endl;
                return 1;
            }
            device_path = argv[++i];
        } else if (arg == "--ep_proxy") {
            enable_ep_proxy = true;
        } else if (arg == "-h" || arg == "--help") {
            cout << "Usage: " << argv[0] << " [-d /dev/videoX] [--ep_proxy]" << endl;
            cout << "Default device: /dev/video0" << endl;
            cout << "  --ep_proxy : enable PCIe frame-sharing bridge (EpProxyBridge)" << endl;
            return 0;
        } else {
            cerr << "Unknown option: " << arg << endl;
            cerr << "Usage: " << argv[0] << " [-d /dev/videoX] [--ep_proxy]" << endl;
            return 1;
        }
    }

    cout << "Opening " << device_path << endl;

    processImage_test(device_path, enable_ep_proxy);

    return 0;
}
