// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * single_imx623_v4l2_mlpp_onnx_1080p_drm.cpp
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
#include "onnx_yolo8n_helper.h"

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

// Ensure the runtime environment required by the ONNX Runtime / VOE / flexmlrt
// stack is set before those libraries are resolved.
static void ensure_runtime_environment(char* argv[]) {
    if (getenv("ECP_ENV_READY")) {
        return; // Already re-exec'd with the environment in place.
    }

    // Prepend our library directories, preserving any existing LD_LIBRARY_PATH.
    const char* required_lib_path =
        "/usr/lib/python3.12/site-packages/voe/lib/:"
        "/usr/lib/python3.12/site-packages/flexmlrt/lib/:"
        "/lib/python3.12/site-packages/onnxruntime/capi/";
    std::string lib_path = required_lib_path;
    if (const char* existing = getenv("LD_LIBRARY_PATH")) {
        if (existing[0] != '\0') {
            lib_path += ":";
            lib_path += existing;
        }
    }

    setenv("XRT_AIARM", "true", 1);
    setenv("LD_LIBRARY_PATH", lib_path.c_str(), 1);
    setenv("XLNX_ENABLE_CACHE", "0", 1);
    setenv("XRT_ELF_FLOW", "1", 1);
    setenv("ECP_ENV_READY", "1", 1);

    // Re-exec so the dynamic loader picks up the new LD_LIBRARY_PATH.
    execv("/proc/self/exe", argv);

    // If execv returns it failed; continue as a best effort (the runtime-only
    // variables above are still in place for libraries loaded via dlopen()).
    perror("execv (re-exec for runtime environment) failed");
}


void processImage_test(const char* device_path, bool enable_ep_proxy) {
    int count = 0;
    int count_main = 0; // Counter for FPS calculation

    MlppHelper mlpp_svc0_in("/dev/mlpp_svc0_in");
    mlpp_svc0_in.update_mlpp_input_params( 1920, 1536);

    MlppHelper mlpp_svc0_display("/dev/mlpp_svc0_display");
    mlpp_svc0_display.update_mlpp_output_params( 1350, 1080, 0, 1920, 0, 1536,  255, 255, 255, 0.0, 1, 4); // NV12

    MlppHelper mlpp_svc0_out("/dev/mlpp_svc0_out");
    mlpp_svc0_out.update_mlpp_output_params(512, 512, 0, 1920, 0, 1536, 255, 255, 255, 0.0, 0, 2); //RGBX

    OnnxYolo8nConfig yolo_cfg;
    OnnxYolo8nHelper yolo(yolo_cfg);
    bool yolo_ok = yolo.init();
    if (!yolo_ok) {
        cout << "YOLOv8 detector init failed; running without detections." << endl;
    }

    // PCIe frame-sharing bridge (MDB5 endpoint). Publishes each captured frame
    // to the host over PCIe (BRAM mailbox + MSI-X) in addition to local use.
    // Only initialized when --ep_proxy is requested.
    EpProxyBridge ep_bridge;
    bool ep_ok = false;
    if (enable_ep_proxy) {
        ep_ok = ep_bridge.init(1); /* channels */
        if (!ep_ok) {
            cout << "PCIe frame sharing disabled (bridge init failed); local-only." << endl;
        }
    } else {
        cout << "PCIe frame sharing disabled (--ep_proxy not set); local-only." << endl;
    }

    /// Crop and scale parameters end
    DrmHelper displayoutput(1920, 1080, 60, DRM_OUT_NV12);
    cout << "Video Stream started " << endl;

    sleep(1);

    // Start non-blocking display thread
    std::thread display_thread(displayThread, &displayoutput);

    cout << "Camera streaming started " << endl;

    std::string fps_string_camera0 = "Camera 0";

    // Use V4L2Camera for direct DMABUF access instead of OpenCV VideoCapture
    V4L2Camera camera0;
    
    // Open camera device with DMABUF support
    if (!camera0.open(device_path, 1920, 1536)) {
        cerr << "Failed to open " << device_path << endl;
        return;
    }

    // Share this camera on mailbox channel 0 (0,1,2,3 are 1920x1536 channels, 4 is 4K channel)
    if (ep_ok) camera0.setEpProxy(&ep_bridge, 0);
    
    // Start streaming
    if (!camera0.startStreaming()) {
        cerr << "Failed to start streaming" << endl;
        return;
    }
    
    cout << "Camera0 opened successfully using " << camera0.getMemoryTypeString() << endl;

    // All channels registered publish the mailbox header magic 
    // so the PCIe host can begin frame discovery.
    if (ep_ok) ep_bridge.finalize();


    cv::Mat svc_display_output(1080 + 1080/2, 1350, CV_8UC1, cv::Scalar(0));
    // Black NV12 default: Y plane = 0 (no luma), UV plane = 128 (neutral chroma).
    // A single Scalar can't express this, so fill the two planes separately.
    cv::Mat nv12_display_output(1080 + 1080/2, 1920, CV_8UC1);
    nv12_display_output(cv::Rect(0, 0, 1920, 1080)).setTo(cv::Scalar(0));     // Y
    nv12_display_output(cv::Rect(0, 1080, 1920, 1080/2)).setTo(cv::Scalar(128)); // UV
    cv::Mat svc0_output(512, 512, CV_8UC4);

    std::vector<YoloDetection> yolo_dets;
    
    auto start_time = high_resolution_clock::now();
    sleep(1);   
    while(1)
    {

        // Capture frame using V4L2Camera with DMABUF support
        bool got_frame0 = camera0.captureFrameDmabuf( mlpp_svc0_in );
        
        if (got_frame0) {
            // read the scaled output NV12 image
            mlpp_svc0_display.read_image_from_mlpp(&svc_display_output);

            // Run yolo detections on the 512x512 scaled RGBX image
            mlpp_svc0_out.read_image_from_mlpp(&svc0_output);
            yolo_dets = yolo.detect(svc0_output);
            // Draw overlay of detections
            OnnxYolo8nHelper::drawDetectionsNV12(svc_display_output, 1350, 1080, yolo_dets, 512, 512);

            // Add label for Camera 0
            cv::putText(svc_display_output, fps_string_camera0, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);

            
            displayoutput.overlay_nv12_data_2_nv12(0, 0, svc_display_output, nv12_display_output);

            // Queue the tiled output for display
            queueFrameForDisplay(nv12_display_output);
            count++;
        }
            

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
    std::cout << "ML preprocessing device closed." << std::endl;

    // Stop all cameras
    camera0.stop();
    
    cout << "All cameras stopped" << endl;

    return;
}


int main(int argc, char* argv[]) {
    // Set up (and re-exec with) the runtime environment the ONNX/VOE stack
    // needs before any of its libraries are resolved. Must run first.
    ensure_runtime_environment(argv);
    
    const char* device_path = "/dev/video0";

    // Parse optional flags and positional camera_id.
    //   --no-display : skip DRM display output (capture/log only)
    //   --ep_proxy   : enable PCIe frame-sharing bridge (EpProxyBridge)
    //   camera_id    : 0..3 selects which /dev/video* to open
    bool enable_ep_proxy = false;

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
        } else if (arg == "--ep_proxy") {
            enable_ep_proxy = true;            
        } else {
            cerr << "Unknown option: " << arg << endl;
            cerr << "Usage: " << argv[0] << " [-d /dev/videoX]" << endl;
            return 1;
        }
    }
    cout << "Opening " << device_path << endl;

    processImage_test(device_path, enable_ep_proxy);

    return 0;
}
