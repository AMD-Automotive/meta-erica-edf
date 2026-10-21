// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * twelve_imx623_dmabuf.cpp
 * Example camera capture application using DMABUF/V4L2Camera for 12x IMX623 cameras
 * on /dev/video0, /dev/video2, /dev/video4, /dev/video6, /dev/video10, /dev/video12,
 * /dev/video14, /dev/video16, /dev/video20, /dev/video22, /dev/video24, /dev/video26.
 * Captures NV12 frames with jitter buffering and tiles them (480x360 each)
 * onto a 1920x1080 display via DRM in a 4x3 layout.
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
#include <array>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <sstream>

#include "drm_helper.h"
#include "isp_helper.h"

using namespace std;
using namespace std::chrono;

static constexpr int NUM_CAMERAS = 12;
static constexpr int SRC_W = 1920;
static constexpr int SRC_H = 1536;
static constexpr int TILE_W = 480;      // 1920 / 4 columns
static constexpr int TILE_H = 360;      // 1080 / 3 rows
static constexpr int OUT_W = 1920;
static constexpr int OUT_H = 1080;

// Global state for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
cv::Mat display_frame;
bool new_frame_available = false;

// Non-blocking display thread function
void displayThread(DrmHelper* displayoutput) {
    cv::Mat local_frame;

    while (display_running) {
        bool has_frame = false;

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (new_frame_available) {
                display_frame.copyTo(local_frame);
                new_frame_available = false;
                has_frame = true;
            }
        }

        if (has_frame && !local_frame.empty()) {
            displayoutput->DrmWriteOutputVSync(&local_frame);
        }

        usleep(1000); // 1ms
    }
}

void queueFrameForDisplay(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.copyTo(display_frame);
    new_frame_available = true;
}

// Shared slot holding the latest BGR tile (TILE_W x TILE_H) from one camera.
struct TileSlot {
    std::mutex mtx;
    cv::Mat tile; // TILE_H x TILE_W, CV_8UC3
    std::atomic<bool> updated{false};
    std::atomic<uint64_t> frame_count{0};
    std::atomic<double> fps{0.0};
};

// Per-camera capture thread: reads NV12 frames from V4L2Camera using the jitter
// buffer, converts to BGR and resizes to TILE_W x TILE_H, then publishes to
// the shared tile slot.
void cameraThread(V4L2Camera* camera, int cam_id, TileSlot* slot,
                  std::atomic<bool>* running) {
    cv::Mat frame_nv12;
    cv::Mat bgr_full;
    cv::Mat tile_local(TILE_H, TILE_W, CV_8UC3);
    struct timeval ts;

    auto start_time = high_resolution_clock::now();
    uint64_t frames_in_window = 0;

    while (*running) {
        // Capture NV12 frame with jitter buffering. This call self-paces to
        // one frame per ~33 ms (configurable via setJitterParams).
        if (!camera->captureFrameDmabufNV12(frame_nv12, &ts)) {
            usleep(1000);
            continue;
        }

        // NV12 (1920x1536) -> BGR (1920x1536), then resize to TILE_W x TILE_H.
        cv::cvtColor(frame_nv12, bgr_full, cv::COLOR_YUV2BGR_NV12);
        cv::resize(bgr_full, tile_local, cv::Size(TILE_W, TILE_H), 0, 0,
                   cv::INTER_LINEAR);

        {
            std::lock_guard<std::mutex> lock(slot->mtx);
            tile_local.copyTo(slot->tile);
            slot->updated = true;
        }
        slot->frame_count++;
        frames_in_window++;

        // Update FPS every ~1 second
        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
        if (elapsed.count() >= 1000) {
            double fps = (elapsed.count() > 0)
                ? (double)frames_in_window * 1000.0 / elapsed.count()
                : 0.0;
            slot->fps.store(fps, std::memory_order_relaxed);
            frames_in_window = 0;
            start_time = high_resolution_clock::now();
        }
    }

    cout << "Camera " << cam_id << " thread exiting" << endl;
}

void processImage_test() {
    DrmHelper displayoutput;
    cout << "Video Stream started" << endl;
    sleep(1);

    std::thread display_thread(displayThread, &displayoutput);
    cout << "Camera streaming started" << endl;

    // Tile slots: one per camera
    std::array<TileSlot, NUM_CAMERAS> slots;
    for (auto& s : slots) {
        s.tile = cv::Mat(TILE_H, TILE_W, CV_8UC3, cv::Scalar(0, 0, 0));
    }

    // Device paths for the 12 cameras
    // Layout: 4 columns x 3 rows
    // Row 0: cameras 0,1,2,3     (/dev/video0, /dev/video2, /dev/video4, /dev/video6)
    // Row 1: cameras 4,5,6,7     (/dev/video10, /dev/video12, /dev/video14, /dev/video16)
    // Row 2: cameras 8,9,10,11   (/dev/video20, /dev/video22, /dev/video24, /dev/video26)
    static const char* device_paths[NUM_CAMERAS] = {
        "/dev/video0",   // camera 0, row 0 col 0
        "/dev/video2",   // camera 1, row 0 col 1
        "/dev/video4",   // camera 2, row 0 col 2
        "/dev/video6",   // camera 3, row 0 col 3
        "/dev/video10",  // camera 4, row 1 col 0
        "/dev/video12",  // camera 5, row 1 col 1
        "/dev/video14",  // camera 6, row 1 col 2
        "/dev/video16",  // camera 7, row 1 col 3
        "/dev/video20",  // camera 8, row 2 col 0
        "/dev/video22",  // camera 9, row 2 col 1
        "/dev/video24",  // camera 10, row 2 col 2
        "/dev/video26",  // camera 11, row 2 col 3
    };

    // Open all cameras SERIALLY on this thread (V4L2/ISP does not tolerate
    // parallel opens). Initialize jitter buffer with 30 fps cadence and 8-frame depth.
    std::array<V4L2Camera, NUM_CAMERAS> cameras;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        cout << "Opening " << device_paths[i] << " (camera " << i << ")..." << endl;
        if (!cameras[i].open(device_paths[i], SRC_W, SRC_H)) {
            cerr << "Aborting: camera " << i << " (" << device_paths[i]
                 << ") failed to open" << endl;
            display_running = false;
            display_thread.join();
            return;
        }

        if (!cameras[i].startStreaming()) {
            cerr << "Aborting: camera " << i << " failed to start streaming" << endl;
            display_running = false;
            display_thread.join();
            return;
        }

        cout << "Camera " << i << " opened at " << SRC_W << "x" << SRC_H
             << ", " << cameras[i].getMemoryTypeString() << endl;
    }

    // Start capture threads (one per camera)
    std::atomic<bool> capture_running{true};
    std::vector<std::thread> capture_threads;
    capture_threads.reserve(NUM_CAMERAS);
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        capture_threads.emplace_back(cameraThread, &cameras[i], i, &slots[i],
                                     &capture_running);
    }

    // Final composed output frame (1920x1080)
    cv::Mat tiled_output(OUT_H, OUT_W, CV_8UC3, cv::Scalar(32, 32, 32));

    // Tile positions: 4 columns x 3 rows
    // Top row (y=0):    col0, col1, col2, col3
    // Mid row (y=360):  col0, col1, col2, col3
    // Bot row (y=720):  col0, col1, col2, col3
    std::array<cv::Point, NUM_CAMERAS> tile_origins;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            int cam_idx = row * 4 + col;
            tile_origins[cam_idx] = cv::Point(col * TILE_W, row * TILE_H);
        }
    }

    std::array<std::string, NUM_CAMERAS> fps_strings;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        std::ostringstream oss;
        oss << "Cam " << i;
        fps_strings[i] = oss.str();
    }

    int frame_count = 0;
    auto stats_time = high_resolution_clock::now();
    sleep(1);

    while (true) {
        // Copy any updated tiles into the composed output.
        for (int i = 0; i < NUM_CAMERAS; ++i) {
            cv::Rect roi(tile_origins[i].x, tile_origins[i].y, TILE_W, TILE_H);
            std::lock_guard<std::mutex> lock(slots[i].mtx);
            if (slots[i].updated) {
                slots[i].tile.copyTo(tiled_output(roi));
                slots[i].updated = false;
            }
        }

        // Draw per-tile FPS labels (smaller font for smaller tiles).
        for (int i = 0; i < NUM_CAMERAS; ++i) {
            cv::putText(tiled_output, fps_strings[i],
                        cv::Point(tile_origins[i].x + 5, tile_origins[i].y + 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 255), 1);
        }

        queueFrameForDisplay(tiled_output);

        usleep(5000); // 5 ms
        frame_count++;

        // Update FPS display every ~1 second
        if (frame_count >= 200) {
            for (int i = 0; i < NUM_CAMERAS; ++i) {
                double fps = slots[i].fps.load(std::memory_order_relaxed);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1);
                oss << "Cam " << i << " " << fps;
                fps_strings[i] = oss.str();
                cout << fps_strings[i] << "  ";
            }
            cout << endl;

            frame_count = 0;
            stats_time = high_resolution_clock::now();
        }
    }

    // Shutdown (not currently reachable in infinite loop; kept for completeness).
    capture_running = false;
    for (auto& t : capture_threads) {
        if (t.joinable()) t.join();
    }

    display_running = false;
    display_thread.join();

    for (int i = 0; i < NUM_CAMERAS; ++i) {
        cameras[i].stop();
    }

    cout << "All cameras stopped" << endl;
}

int main(int /*argc*/, char* /*argv*/[]) {
    cout << "Opening 12x IMX623 cameras:" << endl;
    cout << "  /dev/video0, /dev/video2, /dev/video4, /dev/video6," << endl;
    cout << "  /dev/video10, /dev/video12, /dev/video14, /dev/video16," << endl;
    cout << "  /dev/video20, /dev/video22, /dev/video24, /dev/video26" << endl;
    cout << "Tiling 4x3 (480x360 each) onto 1920x1080 display" << endl;
    processImage_test();
    return 0;
}
