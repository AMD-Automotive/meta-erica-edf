// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * single_imx623_dmabuf_1080p_drm.cpp
 * Example camera capture application taking NV12 from ISP of IMX623 camera
 * Uses V4L2 for the capture
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
#include <vector>
#include <cmath>
#include <limits>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>

#include "drm_helper.h"
#include "isp_helper.h"
#include "ep_proxy_bridge.h"

//typedef unsigned short u16; 

using namespace std;
using namespace std::chrono;

// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;

// Timestamp log accessible from the signal handler so we can flush/close
// (and sync to disk) on Ctrl+C before the process exits.
static std::ofstream* g_ts_log = nullptr;
static std::mutex g_ts_log_mutex;

// Log the user timestamp versus the sensor timestamp from v4l2 buf
#define LOG_TIMESTAMPS 0

static void handle_sigint(int /*signo*/) {
    // Best-effort: flush, close, and sync the timestamp log to disk, then exit.
    // Note: only async-signal-safe calls are strictly safe here; the iostream
    // operations below are not, but this is a single-threaded shutdown path
    // for a small log file and is the common pragmatic pattern.
    {
        std::lock_guard<std::mutex> lock(g_ts_log_mutex);
        if (g_ts_log && g_ts_log->is_open()) {
            g_ts_log->flush();
            g_ts_log->close();
        }
    }
    ::sync(); // ensure data hits the disk
    std::_Exit(0);
}

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

void processImage_test(const char* device_path, bool enable_display,
                       bool enable_ep_proxy) {
    int count = 0;
    int count_main = 0; // Counter for FPS calculation

    /// Crop and scale parameters end
    // DRM display is optional; constructed only when display is enabled.
    std::unique_ptr<DrmHelper> displayoutput;
    std::thread display_thread;
    if (enable_display) {
        displayoutput.reset(new DrmHelper(1920, 1080, 60, DRM_OUT_NV12)); // drm_helper, setups DRM buffer
        cout << "Video Stream started " << endl;
        sleep(1);
        // Start non-blocking display thread
        display_thread = std::thread(displayThread, displayoutput.get());
    } else {
        cout << "Video Stream started (display disabled)" << endl;
        sleep(1);
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

    cout << "Camera streaming started " << endl;

    std::string fps_string_camera = " ";

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

    // Let the driver queue 1 extra frame before the first paced read so there
    camera0.setTargetPeriodMs(33.3333333); // ~30 fps grid    
    // is a buffering cushion (adds ~1 frame of latency). Set 0 to disable.
    camera0.setPacingPrimeFrames(1); 

    
    cout << "Camera (" << device_path << ") opened successfully using "
         << camera0.getMemoryTypeString() << endl;

    // All channels registered publish the mailbox header magic 
    // so the PCIe host can begin frame discovery.
    if (ep_ok) ep_bridge.finalize();


    // NV12 buffer: Y plane (1536 x 1920) + interleaved UV plane (768 x 1920)
    // => stored as a single-channel Mat of (1536 * 3 / 2) rows x 1920 cols.
    const int src_w = 1920;
    const int src_h = 1536;
    cv::Mat frame_nv12(src_h * 3 / 2, src_w, CV_8UC1, cv::Scalar(0));
    cv::Mat frame_bgr_full;
    cv::Mat frame0(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));

    // Center crop to 1920x1080 from the 1920x1536 BGR conversion.
    const int crop_y = (src_h - 1080) / 2; // 228
    const cv::Rect crop_rect(0, crop_y, 1920, 1080);

    auto start_time = high_resolution_clock::now();
    struct timeval ts;

    // Userspace per-frame timing: record interval between consecutive successful
    // captureFrameDmabufNV12() returns. Microseconds, cleared each report window.
    std::vector<double> frame_intervals_us;
    frame_intervals_us.reserve(1024);
    bool have_last_frame_time = false;
    auto last_frame_time = high_resolution_clock::now();

    // Open timestamp log (truncate at start). One line per successful frame:
    // "<sec>.<usec_6digit>"
    // Filename is unique per /dev/video* device, e.g. "frame_timestamps_video0.txt".
#if LOG_TIMESTAMPS
    std::string ts_log_name = "frame_timestamps";
    {
        std::string dev(device_path ? device_path : "");
        const std::string prefix = "/dev/";
        if (dev.compare(0, prefix.size(), prefix) == 0) {
            dev.erase(0, prefix.size());
        }
        // Replace any path separators with underscores to keep it a flat filename.
        for (char& c : dev) {
            if (c == '/' || c == '\\') c = '_';
        }
        if (!dev.empty()) {
            ts_log_name += "_" + dev;
        }
        ts_log_name += ".txt";
    }
    std::ofstream ts_log(ts_log_name.c_str(), std::ios::out | std::ios::trunc);
    if (!ts_log.is_open()) {
        cerr << "WARNING: failed to open " << ts_log_name << " for writing" << endl;
    } else {
        cout << "Logging frame timestamps to " << ts_log_name << endl;
    }

    // Publish ts_log to the SIGINT handler and install it so Ctrl+C flushes
    // and syncs the log to disk before exiting.
    {
        std::lock_guard<std::mutex> lock(g_ts_log_mutex);
        g_ts_log = &ts_log;
    }
#endif

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART: let blocking calls return EINTR if needed
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sleep(1);   
    while(1)
    {

        // Capture frame using V4L2Camera with DMABUF support
        bool got_frame0 = camera0.captureFrameDmabufNV12( frame_nv12, &ts, true );
        

        
        if (got_frame0) {
            // Userspace timestamp for this frame and inter-frame delta.
            auto now_frame_time = high_resolution_clock::now();
            if (have_last_frame_time) {
                double dt_us = duration_cast<microseconds>(
                    now_frame_time - last_frame_time).count();
                frame_intervals_us.push_back(dt_us);
            }
            last_frame_time = now_frame_time;
            have_last_frame_time = true;


            // Log line: "<system_sec>.<system_usec> <boot_sec>.<boot_usec> <v4l2_sec>.<v4l2_usec>"
            // system time = wall-clock at the moment we observed the frame (userspace).
            // boot time   = CLOCK_MONOTONIC seconds since boot (userspace).
            // v4l2 time   = buf.timestamp filled by the kernel for the captured frame.
#if LOG_TIMESTAMPS
            if (ts_log.is_open()) {
                struct timeval now_sys;
                gettimeofday(&now_sys, nullptr);

                struct timespec now_boot;
                clock_gettime(CLOCK_MONOTONIC, &now_boot);
                long boot_usec = now_boot.tv_nsec / 1000;

                ts_log << now_sys.tv_sec << '.'
                       << std::setw(6) << std::setfill('0') << now_sys.tv_usec
                       << std::setfill(' ') << ' '
                       << now_boot.tv_sec << '.'
                       << std::setw(6) << std::setfill('0') << boot_usec
                       << std::setfill(' ') << ' '
                       << ts.tv_sec << '.'
                       << std::setw(6) << std::setfill('0') << ts.tv_usec
                       << std::setfill(' ') << '\n';
            }
#endif

            // Center-crop NV12 1920x1536 -> 1920x1080 by copying Y and UV
            // planes separately (crop_y=228 is even, so chroma stays aligned).
            cv::Mat frame_nv12_crop(1080 * 3 / 2, src_w, CV_8UC1);
            frame_nv12(cv::Rect(0, crop_y, src_w, 1080))
                .copyTo(frame_nv12_crop(cv::Rect(0, 0, src_w, 1080)));
            frame_nv12(cv::Rect(0, src_h + crop_y / 2, src_w, 1080 / 2))
                .copyTo(frame_nv12_crop(cv::Rect(0, 1080, src_w, 1080 / 2)));

            // Draw the label directly on the NV12 Y (luma) plane.
            int luma_height = frame_nv12_crop.rows * 2 / 3;
            cv::Mat y_plane = frame_nv12_crop(cv::Rect(0, 0, frame_nv12_crop.cols, luma_height));
            cv::putText(y_plane, fps_string_camera, cv::Point(10, 30),
                       cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255), 2);

            // Queue the tiled output for display (only when display is enabled)
            if (enable_display) {
                queueFrameForDisplay(frame_nv12_crop);
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
            fps_stream << std::fixed << std::setprecision(2) << "Camera 0 - FPS: " << fps;
            fps_string_camera = fps_stream.str();

            // Compute inter-frame interval stats over this reporting window.
            double dt_min_ms = 0.0, dt_max_ms = 0.0, dt_mean_ms = 0.0, dt_std_ms = 0.0;
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
                 << "cameras " << fps
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
    std::cout << "ML preprocessing device closed." << std::endl;

#if LOG_TIMESTAMPS
    if (ts_log.is_open()) {
        ts_log.flush();
        ts_log.close();
    }
    ::sync();
    {
        std::lock_guard<std::mutex> lock(g_ts_log_mutex);
        g_ts_log = nullptr;
    }
#endif
    // Stop all cameras
    camera0.stop();
    
    cout << "All cameras stopped" << endl;

    return;
}


int main(int argc, char* argv[]) {
    // Parse optional flags and positional camera_id.
    //   --no-display : skip DRM display output (capture/log only)
    //   --ep_proxy   : enable PCIe frame-sharing bridge (EpProxyBridge)
    //   camera_id    : 0..3 selects which /dev/video* to open
    bool enable_display = true;
    bool enable_ep_proxy = false;
    int positional_count = 0;
    const char* camera_id_arg = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-display") {
            enable_display = false;
        } else if (a == "--ep_proxy") {
            enable_ep_proxy = true;
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
        cerr << "Usage: " << argv[0] << " [--no-display] [--ep_proxy] <camera_id>" << endl;
        cerr << "  --no-display : skip DRM display output (capture/log only)" << endl;
        cerr << "  --ep_proxy   : enable PCIe frame-sharing bridge (EpProxyBridge)" << endl;
        cerr << "  camera_id    : 0..11" << endl;
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

    int camera_id = atoi(camera_id_arg);
    if (camera_id < 0 || camera_id > 11) {
        cerr << "Error: camera_id must be in range 0..11" << endl;
        return 1;
    }

    static const char* device_paths[12] = {
        "/dev/video0",
        "/dev/video2",
        "/dev/video4",
        "/dev/video6",
        "/dev/video10",
        "/dev/video12",
        "/dev/video14",
        "/dev/video16",
        "/dev/video20",
        "/dev/video22",
        "/dev/video24",
        "/dev/video26",
    };
    const char* device_path = device_paths[camera_id];

    cout << "Opening " << device_path << " (camera_id: " << camera_id
         << ", display: " << (enable_display ? "on" : "off")
         << ", ep_proxy: " << (enable_ep_proxy ? "on" : "off") << ")" << endl;

    //processImage();
    processImage_test(device_path, enable_display, enable_ep_proxy);

    return 0;
}
