// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * quad_imx623_dmabuf.cpp
 * Example camera capture application using DMABUF/V4L2Camera for 4x IMX623 cameras
 * on /dev/video0, /dev/video2, /dev/video4, /dev/video6.
 * Captures NV12 frames with jitter buffering and tiles them (960x540 each)
 * onto a 1920x1080 display via DRM in a 2x2 layout.
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

static constexpr int NUM_CAMERAS = 4;
static constexpr int SRC_W = 1920;
static constexpr int SRC_H = 1536;
// Each camera frame is center-cropped to 1920x1080 (NV12) before tiling.
static constexpr int CROP_W = 1920;
static constexpr int CROP_H = 1080;
static constexpr int CROP_Y = (SRC_H - CROP_H) / 2; // 228 (even -> chroma aligned)
// 4K NV12 output, arranged as a 2x2 grid of 1920x1080 tiles.
static constexpr int OUT_W = 3840;
static constexpr int OUT_H = 2160;

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
            displayoutput->DrmWriteOutputNV12VSync(&local_frame);
            //displayoutput->DrmWriteOutputNV12(&local_frame);
            
        }

        usleep(1000); // 1ms
    }
}

void queueFrameForDisplay(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.copyTo(display_frame);
    new_frame_available = true;
}

// Shared slot holding the latest center-cropped NV12 tile (1920x1080) from one camera.
// Shared slot holding the latest center-cropped NV12 tile (1920x1080) from one
// camera, using a lock-free single-producer/single-consumer triple buffer.
//
// Three buffers rotate between the capture thread (producer) and the compositor
// (consumer) so neither ever reads a buffer the other is writing, and no copy
// or mutex is needed for the handoff:
//   - producer_idx : buffer the capture thread writes into (producer-owned)
//   - consumer_idx : buffer the compositor reads from (consumer-owned)
//   - middle       : atomic exchange slot; low bits hold an index, FRESH bit
//                    marks that the producer published new data.
struct TileSlot {
    static constexpr unsigned INDEX_MASK = 0x3;
    static constexpr unsigned FRESH      = 0x4;

    cv::Mat buf[3]; // three packed NV12 tiles, (CROP_H*3/2) x CROP_W, CV_8UC1
    std::atomic<unsigned> middle{2u}; // starts holding index 2, not fresh
    int producer_idx = 0;             // capture-thread-owned
    int consumer_idx = 1;             // compositor-owned
    std::atomic<uint64_t> frame_count{0};
    std::atomic<double> fps{0.0};

    // Producer: publish the just-written producer buffer and take ownership of
    // whatever buffer was sitting in the middle slot.
    void publish() {
        unsigned old = middle.exchange(
            static_cast<unsigned>(producer_idx) | FRESH, std::memory_order_acq_rel);
        producer_idx = static_cast<int>(old & INDEX_MASK);
    }

    // Consumer: if the producer published new data, swap our consumer buffer
    // into the middle slot and take the fresh one. Returns true if a new frame
    // is now available in buf[consumer_idx].
    bool fetch() {
        if (!(middle.load(std::memory_order_acquire) & FRESH)) {
            return false;
        }
        unsigned old = middle.exchange(
            static_cast<unsigned>(consumer_idx), std::memory_order_acq_rel);
        consumer_idx = static_cast<int>(old & INDEX_MASK);
        return true;
    }
};

// Per-camera capture thread: reads NV12 frames from V4L2Camera, center-crops
// each 1920x1536 frame to 1920x1080 (in NV12, no color conversion) directly
// into its producer buffer, then publishes it via the triple buffer.
void cameraThread(V4L2Camera* camera, int cam_id, TileSlot* slot,
                  std::atomic<bool>* running) {
    cv::Mat frame_nv12;
    struct timeval ts;

    auto start_time = high_resolution_clock::now();
    uint64_t frames_in_window = 0;

    while (*running) {
        // Capture NV12 frame. Pace == true holds the return to the configured
        // period (set via setTargetPeriodMs) so the camera runs just under the
        // 30 Hz display rate.
        if (!camera->captureFrameDmabufNV12(frame_nv12, &ts, false)) {
            usleep(1000);
            continue;
        }

        // Center-crop NV12 1920x1536 -> 1920x1080 by copying the Y and UV
        // planes separately (CROP_Y=228 is even, so chroma stays aligned)
        // directly into the producer-owned buffer.
        cv::Mat& dst = slot->buf[slot->producer_idx];
        frame_nv12(cv::Rect(0, CROP_Y, CROP_W, CROP_H))
            .copyTo(dst(cv::Rect(0, 0, CROP_W, CROP_H)));
        frame_nv12(cv::Rect(0, SRC_H + CROP_Y / 2, CROP_W, CROP_H / 2))
            .copyTo(dst(cv::Rect(0, CROP_H, CROP_W, CROP_H / 2)));

        slot->publish();
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

void processImage_test(const char* const* device_paths) {
    DrmHelper displayoutput(3840, 2160, 30, DRM_OUT_NV12);
    cout << "Video Stream started" << endl;
    sleep(1);

    std::thread display_thread(displayThread, &displayoutput);
    cout << "Camera streaming started" << endl;

    // Tile slots: one per camera. Each owns three 1920x1080 NV12 buffers
    // initialized to neutral (Y=0, UV=128).
    std::array<TileSlot, NUM_CAMERAS> slots;
    for (auto& s : slots) {
        for (auto& b : s.buf) {
            b = cv::Mat(CROP_H * 3 / 2, CROP_W, CV_8UC1);
            b(cv::Rect(0, 0, CROP_W, CROP_H)).setTo(0);            // Y
            b(cv::Rect(0, CROP_H, CROP_W, CROP_H / 2)).setTo(128); // UV
        }
    }

    // Open all cameras SERIALLY on this thread (V4L2/ISP does not tolerate
    // parallel opens). 
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

        cameras[i].setTargetPeriodMs(33.3333333);
        cameras[i].setPacingPrimeFrames(1); 

        cout << "Camera " << i << " opened at " << SRC_W << "x" << SRC_H
             << ", " << cameras[i].getMemoryTypeString() << endl;
        sleep(1);
    }

    // Start capture threads (one per camera)
    std::atomic<bool> capture_running{true};
    std::vector<std::thread> capture_threads;
    capture_threads.reserve(NUM_CAMERAS);
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        capture_threads.emplace_back(cameraThread, &cameras[i], i, &slots[i],
                                     &capture_running);
    }

    // Final composed 4K NV12 output frame (3840x2160), neutral-initialized.
    cv::Mat tiled_output(OUT_H * 3 / 2, OUT_W, CV_8UC1);
    tiled_output(cv::Rect(0, 0, OUT_W, OUT_H)).setTo(0);            // Y plane
    tiled_output(cv::Rect(0, OUT_H, OUT_W, OUT_H / 2)).setTo(128);  // UV plane

    // Tile positions (top-left origin of each 1920x1080 quadrant in Y-plane
    // pixel coordinates): top-left, top-right, bottom-left, bottom-right.
    const std::array<cv::Point, NUM_CAMERAS> tile_origins = {
        cv::Point(0, 0),            // camera 0: top-left
        cv::Point(CROP_W, 0),       // camera 1: top-right
        cv::Point(0, CROP_H),       // camera 2: bottom-left
        cv::Point(CROP_W, CROP_H)   // camera 3: bottom-right
    };

    std::array<std::string, NUM_CAMERAS> fps_strings = {
        std::string("Camera 0"),
        std::string("Camera 1"),
        std::string("Camera 2"),
        std::string("Camera 3")
    };

    int frame_count = 0;
    auto stats_time = high_resolution_clock::now();

    // Throttle full-frame pushes to the display refresh rate. Tiles are
    // composited cheaply as they arrive, but the expensive 12 MB push to the
    // display happens at most once per display period. The panel is 4K30, so
    // push at ~30 Hz; the actual scanout is VSync-paced by
    // DrmWriteOutputNV12VSync.
    const auto push_period = microseconds(33333); // ~30 Hz, matches 4K30 mode
    auto next_push = high_resolution_clock::now();
    bool dirty_since_push = false;
    sleep(1);

    while (true) {
        // Composite any freshly published NV12 tiles into the 4K output. These
        // per-tile copies are cheap (a few MB) compared with the full-frame
        // push, so we do them every iteration to keep tiles current.
        for (int i = 0; i < NUM_CAMERAS; ++i) {
            const int qx = tile_origins[i].x;
            const int qy = tile_origins[i].y;
            // Lock-free: pick up the latest published tile, if any.
            if (slots[i].fetch()) {
                const cv::Mat& tile = slots[i].buf[slots[i].consumer_idx];
                // Y plane: place tile Y at the quadrant origin.
                tile(cv::Rect(0, 0, CROP_W, CROP_H))
                    .copyTo(tiled_output(cv::Rect(qx, qy, CROP_W, CROP_H)));
                // UV plane: output UV starts at row OUT_H; the quadrant's UV
                // rows are half-height, so the vertical offset is qy/2.
                tile(cv::Rect(0, CROP_H, CROP_W, CROP_H / 2))
                    .copyTo(tiled_output(cv::Rect(qx, OUT_H + qy / 2, CROP_W, CROP_H / 2)));
                dirty_since_push = true;
            }
        }

        // Push to the display at most once per display period, and only if a
        // tile changed since the last push (the display is VSync-paced, so
        // re-pushing an identical frame gains nothing).
        auto now = high_resolution_clock::now();
        if (dirty_since_push && now >= next_push) {
            // Draw per-tile FPS labels on the Y (luma) plane only.
            cv::Mat y_plane = tiled_output(cv::Rect(0, 0, OUT_W, OUT_H));
            for (int i = 0; i < NUM_CAMERAS; ++i) {
                cv::putText(y_plane, fps_strings[i],
                            cv::Point(tile_origins[i].x + 10, tile_origins[i].y + 50),
                            cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(255), 3);
            }

            queueFrameForDisplay(tiled_output);
            dirty_since_push = false;
            // Advance the deadline without drift; resync if we fell far behind.
            next_push += push_period;
            if (next_push < now) next_push = now + push_period;
        }

        // Pace the composite loop. A short sleep keeps CPU/memory bandwidth
        // free for the four capture threads while still polling tiles often
        // enough (~1 ms) to pick up new frames promptly.
        usleep(1000);

        frame_count++;

        // Update FPS display every ~1 second
        if (frame_count >= 200) {
            for (int i = 0; i < NUM_CAMERAS; ++i) {
                double fps = slots[i].fps.load(std::memory_order_relaxed);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "Camera " << i << " - FPS: " << fps;
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

int main(int argc, char* argv[]) {
    // Deserializer group selection. Each group maps to 4 /dev/video* nodes:
    //   DES0 -> /dev/video0,2,4,6      (default)
    //   DES1 -> /dev/video10,12,14,16
    //   DES2 -> /dev/video20,22,24,26
    static const char* const des_groups[3][NUM_CAMERAS] = {
        { "/dev/video0",  "/dev/video2",  "/dev/video4",  "/dev/video6"  }, // DES0
        { "/dev/video10", "/dev/video12", "/dev/video14", "/dev/video16" }, // DES1
        { "/dev/video20", "/dev/video22", "/dev/video24", "/dev/video26" }, // DES2
    };

    std::string sel = (argc > 1) ? argv[1] : "DES0";
    int group = -1;
    if (sel == "DES0") group = 0;
    else if (sel == "DES1") group = 1;
    else if (sel == "DES2" || sel == "DES") group = 2;
    else {
        cerr << "Unknown argument: " << sel << endl;
        cerr << "Usage: " << argv[0] << " [DES0|DES1|DES2]" << endl;
        cerr << "  DES0 -> /dev/video0,2,4,6 (default)" << endl;
        cerr << "  DES1 -> /dev/video10,12,14,16" << endl;
        cerr << "  DES2 -> /dev/video20,22,24,26" << endl;
        return 1;
    }

    const char* const* device_paths = des_groups[group];

    cout << "Opening 4x IMX623 cameras (" << sel << "):" << endl;
    cout << "  " << device_paths[0] << ", " << device_paths[1] << ", "
         << device_paths[2] << ", " << device_paths[3] << endl;
    cout << "Center-cropping each 1920x1536 NV12 stream to 1920x1080" << endl;
    cout << "Tiling 2x2 onto a 3840x2160 NV12 4K display" << endl;
    processImage_test(device_paths);
    return 0;
}
