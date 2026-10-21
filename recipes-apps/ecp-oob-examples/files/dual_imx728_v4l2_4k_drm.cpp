// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * dual_imx728_v4l2_4k_drm.cpp
 * Example capture application using DMABUF/V4L2Camera for 2x IMX728 cameras
 * on /dev/video0 and /dev/video2.
 * Captures 3840x2160 NV12 frames, scales each to 1920x1080 (aspect preserved)
 * and displays them side by side on a 3840x2160 NV12 DRM output:
 *   left half  = /dev/video0
 *   right half = /dev/video2
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

static constexpr int NUM_CAMERAS = 2;
// Each camera streams 4K NV12.
static constexpr int SRC_W = 3840;
static constexpr int SRC_H = 2160;
// Each 4K frame is scaled down to a 1920x1080 tile (aspect preserved).
static constexpr int TILE_W = 1920;
static constexpr int TILE_H = 1080;
// 4K NV12 output: the two 1920x1080 tiles sit side by side, centered
// vertically (black bars top/bottom keep the 16:9 aspect ratio).
static constexpr int OUT_W = 3840;
static constexpr int OUT_H = 2160;
static constexpr int TILE_Y = (OUT_H - TILE_H) / 2; // 540 (even -> chroma aligned)

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
        }

        usleep(1000); // 1ms
    }
}

void queueFrameForDisplay(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    frame.copyTo(display_frame);
    new_frame_available = true;
}

// Resize a packed NV12 image (src_h*3/2 x src_w, CV_8UC1) into a packed NV12
// image (dst_h*3/2 x dst_w, CV_8UC1). The Y plane and the interleaved UV plane
// are resized separately so chroma stays correctly subsampled. All dimensions
// must be even. dst is (re)allocated if its size/type does not already match.
static void resizeNV12(const cv::Mat& src, int src_w, int src_h,
                       cv::Mat& dst, int dst_w, int dst_h) {
    if (dst.empty() || dst.rows != dst_h * 3 / 2 || dst.cols != dst_w ||
        dst.type() != CV_8UC1) {
        dst = cv::Mat(dst_h * 3 / 2, dst_w, CV_8UC1);
    }

    // Y plane: full-resolution luma.
    cv::Mat src_y = src(cv::Rect(0, 0, src_w, src_h));
    cv::Mat dst_y = dst(cv::Rect(0, 0, dst_w, dst_h));
    cv::resize(src_y, dst_y, cv::Size(dst_w, dst_h), 0, 0, cv::INTER_LINEAR);

    // Interleaved UV plane: view the packed bytes as 2-channel (U,V) pairs at
    // half resolution and resize as a single CV_8UC2 image.
    cv::Mat src_uv(src_h / 2, src_w / 2, CV_8UC2,
                   src.data + (size_t)src_h * src.step[0], src.step[0]);
    cv::Mat dst_uv(dst_h / 2, dst_w / 2, CV_8UC2,
                   dst.data + (size_t)dst_h * dst.step[0], dst.step[0]);
    cv::resize(src_uv, dst_uv, cv::Size(dst_w / 2, dst_h / 2), 0, 0,
               cv::INTER_LINEAR);
}

// Lock-free single-producer/single-consumer triple buffer holding the latest
// scaled NV12 tile (1920x1080) from one camera. Three buffers rotate between
// the capture thread (producer) and the compositor (consumer) so neither ever
// reads a buffer the other is writing, and no mutex is needed for the handoff:
//   - producer_idx : buffer the capture thread writes into (producer-owned)
//   - consumer_idx : buffer the compositor reads from (consumer-owned)
//   - middle       : atomic exchange slot; low bits hold an index, FRESH bit
//                    marks that the producer published new data.
struct TileSlot {
    static constexpr unsigned INDEX_MASK = 0x3;
    static constexpr unsigned FRESH      = 0x4;

    cv::Mat buf[3]; // three packed NV12 tiles, (TILE_H*3/2) x TILE_W, CV_8UC1
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

// Per-camera capture thread: reads 4K NV12 frames from V4L2Camera, scales each
// 3840x2160 frame down to a 1920x1080 NV12 tile directly into its producer
// buffer, then publishes it via the triple buffer.
void cameraThread(V4L2Camera* camera, int cam_id, TileSlot* slot,
                  std::atomic<bool>* running) {
    cv::Mat frame_nv12;
    struct timeval ts;

    auto start_time = high_resolution_clock::now();
    uint64_t frames_in_window = 0;

    while (*running) {
        if (!camera->captureFrameDmabufNV12(frame_nv12, &ts, false)) {
            usleep(1000);
            continue;
        }

        // Scale the captured 4K NV12 frame into the producer-owned tile.
        resizeNV12(frame_nv12, SRC_W, SRC_H,
                   slot->buf[slot->producer_idx], TILE_W, TILE_H);

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

void processImage_test() {
    DrmHelper displayoutput(OUT_W, OUT_H, 30, DRM_OUT_NV12);
    cout << "Video Stream started" << endl;
    sleep(1);

    std::thread display_thread(displayThread, &displayoutput);
    cout << "Camera streaming started" << endl;

    // Tile slots: one per camera. Each owns three 1920x1080 NV12 buffers
    // initialized to neutral (Y=0, UV=128).
    std::array<TileSlot, NUM_CAMERAS> slots;
    for (auto& s : slots) {
        for (auto& b : s.buf) {
            b = cv::Mat(TILE_H * 3 / 2, TILE_W, CV_8UC1);
            b(cv::Rect(0, 0, TILE_W, TILE_H)).setTo(0);            // Y
            b(cv::Rect(0, TILE_H, TILE_W, TILE_H / 2)).setTo(128); // UV
        }
    }

    // Device paths for the 2 cameras
    static const char* device_paths[NUM_CAMERAS] = {
        "/dev/video4",  // IMX728(0) -> left
        "/dev/video2",  // IMX728(1) -> right
    };

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

    // Tile positions (top-left origin of each 1920x1080 tile in Y-plane pixel
    // coordinates): left half and right half, vertically centered.
    const std::array<cv::Point, NUM_CAMERAS> tile_origins = {
        cv::Point(0,      TILE_Y), // camera 0: left
        cv::Point(TILE_W, TILE_Y)  // camera 1: right
    };

    std::array<std::string, NUM_CAMERAS> fps_strings = {
        std::string("Camera 0"),
        std::string("Camera 1")
    };

    int frame_count = 0;
    auto stats_time = high_resolution_clock::now();

    // Throttle full-frame pushes to the display refresh rate. Tiles are
    // composited cheaply as they arrive, but the expensive 12 MB push to the
    // display happens at most once per display period (4K30); the actual
    // scanout is VSync-paced by DrmWriteOutputNV12VSync.
    const auto push_period = microseconds(33333); // ~30 Hz, matches 4K30 mode
    auto next_push = high_resolution_clock::now();
    bool dirty_since_push = false;
    sleep(1);

    while (true) {
        // Composite any freshly published NV12 tiles into the 4K output.
        for (int i = 0; i < NUM_CAMERAS; ++i) {
            const int tx = tile_origins[i].x;
            const int ty = tile_origins[i].y;
            // Lock-free: pick up the latest published tile, if any.
            if (slots[i].fetch()) {
                const cv::Mat& tile = slots[i].buf[slots[i].consumer_idx];
                // Y plane: place tile Y at the tile origin.
                tile(cv::Rect(0, 0, TILE_W, TILE_H))
                    .copyTo(tiled_output(cv::Rect(tx, ty, TILE_W, TILE_H)));
                // UV plane: output UV starts at row OUT_H; the tile's UV rows
                // are half-height, so the vertical offset is ty/2.
                tile(cv::Rect(0, TILE_H, TILE_W, TILE_H / 2))
                    .copyTo(tiled_output(cv::Rect(tx, OUT_H + ty / 2, TILE_W, TILE_H / 2)));
                dirty_since_push = true;
            }
        }

        // Push to the display at most once per display period, and only if a
        // tile changed since the last push.
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

        // Pace the composite loop so CPU/memory bandwidth stays free for the
        // capture threads while still polling tiles often enough (~1 ms).
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

int main(int /*argc*/, char* /*argv*/[]) {
    cout << "Opening 2x IMX728 cameras:" << endl;
    cout << "  /dev/video0 (left), /dev/video2 (right)" << endl;
    cout << "Scaling each 3840x2160 NV12 stream to 1920x1080" << endl;
    cout << "Displaying side by side on a 3840x2160 NV12 4K display" << endl;
    processImage_test();
    return 0;
}
