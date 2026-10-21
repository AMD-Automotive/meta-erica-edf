// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT


/**
 * vcu_gst_nv12_5vid.cpp
 * GStreamer pipeline with VCU video decode and MLPP multi-crop processing of 5 streams in parallel.
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
#include <signal.h>
#include <atomic>

#include "drm_helper.h"
#include "mlpp_helper.h"
#include "vcu_helper.h"

using namespace std;
using namespace std::chrono;

#define DEBUG 0

// Global flag for graceful shutdown
std::atomic<bool> shutdown_requested{false};
// Global variables for non-blocking display
std::atomic<bool> display_running{true};
std::mutex frame_mutex;
std::mutex mlpp_safeguard_mutex;
int dmafd_svc[4];
int dmafd_svc_size[4];

cv::Mat display_frame;
bool new_frame_available = false;

// Frame synchronization for SVC before FFC writes

std::atomic<int> svc_frames_written{0};
std::atomic<int> ffc_frame_count{0};
std::atomic<int> svc_frame_count[4];
std::atomic<int> svc_frame_count_last[4];

std::atomic<bool> ffc_frame_ready{0};
std::mutex sync_mutex;

// Per-stream EOF tracking
std::atomic<bool> stream_eof[5];
std::atomic<bool> any_stream_eof{false};


cv::Mat LSD1_output(256*3, 608, CV_8UC1);
cv::Mat LSD2_output(288*3, 640, CV_8UC1);
cv::Mat OD1_output(448*3, 1024 , CV_8UC1);
cv::Mat OD2_output(384*3, 2048, CV_8UC1);
cv::Mat LD_output(320*3, 640, CV_8UC1);
cv::Mat SC_output(270*3, 480, CV_8UC1);

cv::Mat svc0_input(1536, 1920, CV_8UC3, cv::Scalar(128, 128, 128));

// Signal handler function
void signal_handler(int signum) {
    g_print("Signal %d received, initiating graceful shutdown...\n", signum);
    shutdown_requested = true;
    display_running = false;
}

// Setup signal handlers
void setup_signal_handlers() {
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // Termination request
    //signal(SIGABRT, signal_handler);  // Abort signal
    signal(SIGQUIT, signal_handler);  // Quit signal
}

// Non-blocking display thread function
void displayThread(DrmHelper* displayoutput) {
    cv::Mat local_frame;
    
    while (display_running && !shutdown_requested) {
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


// GStreamer new sample callback
static GstFlowReturn new_sample_callback(GstAppSink *appsink, gpointer user_data) {
    // Check for shutdown at the beginning
    if (shutdown_requested || any_stream_eof.load()) {
        return GST_FLOW_FLUSHING; // Indicate we're shutting down
    }

    GstData *data = (GstData*)user_data;
    GstSample *sample = NULL;
    GstBuffer *buffer = NULL;
    GstCaps *caps = NULL;
    GstVideoInfo video_info;
    GstFlowReturn ret = GST_FLOW_OK;
    GstMemory *mem = NULL;
    int dmabuf_fd = -1;

    sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        // No sample available - could indicate EOF for this stream
        GstData *data = (GstData*)user_data;
        if (data && data->video_index >= 0 && data->video_index < 5) {
            stream_eof[data->video_index].store(true);
            any_stream_eof.store(true);
            g_print("Stream %d reached EOF (no sample available)\n", data->video_index);
        }
        return shutdown_requested || any_stream_eof.load() ? GST_FLOW_FLUSHING : GST_FLOW_EOS;
    }

    buffer = gst_sample_get_buffer(sample);
    caps = gst_sample_get_caps(sample);

    if (!gst_video_info_from_caps(&video_info, caps)) {
        ret = GST_FLOW_ERROR;
        goto cleanup;
    }

    // Debug: Print format information for DMABUF
    static bool format_logged = false;
    if (!format_logged) {
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("DMABUF Format Debug:\n");
        g_print("  Caps: %s\n", caps_str);
        g_print("  Format: %s\n", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&video_info)));
        g_print("  Width: %d, Height: %d\n", GST_VIDEO_INFO_WIDTH(&video_info), GST_VIDEO_INFO_HEIGHT(&video_info));
        g_print("  Framerate: %d/%d\n", GST_VIDEO_INFO_FPS_N(&video_info), GST_VIDEO_INFO_FPS_D(&video_info));
        g_print("  Size: %zu bytes\n", GST_VIDEO_INFO_SIZE(&video_info));
        
        // NV12 specific debugging
        if (GST_VIDEO_INFO_FORMAT(&video_info) == GST_VIDEO_FORMAT_NV12) {
            g_print("  NV12 Format Details:\n");
            g_print("    Y-plane stride: %d\n", GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0));
            g_print("    UV-plane stride: %d\n", GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 1));
            g_print("    Y-plane offset: %zu\n", GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 0));
            g_print("    UV-plane offset: %zu\n", GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 1));
        }
        
        g_free(caps_str);
        format_logged = true;
    }

    // Check for shutdown or any stream EOF before processing
    if (shutdown_requested || any_stream_eof.load()) {
        ret = GST_FLOW_FLUSHING;
        goto cleanup;
    }
   
    mem = gst_buffer_peek_memory(buffer, 0);
    if (mem && gst_is_dmabuf_memory(mem)) {
        dmabuf_fd = gst_dmabuf_memory_get_fd(mem);
        
        // Debug DMABUF memory details
        static int dmabuf_debug_logged = 5;
        if (dmabuf_debug_logged > 0) {
            gsize offset, maxsize;
            gsize size = gst_memory_get_sizes(mem, &offset, &maxsize);
            g_print("DMABUF Memory Debug:\n");
            g_print("  DMABUF FD: %d\n", dmabuf_fd);
            g_print("  Memory size: %zu bytes\n", size);
            g_print("  Memory offset: %zu\n", offset);
            g_print("  Memory maxsize: %zu\n", maxsize);
            g_print("  Buffer n_memory: %u\n", gst_buffer_n_memory(buffer));
            dmabuf_debug_logged--;
        }
        
        if (dmabuf_fd < 0) {
            g_print("Error: Invalid DMABUF FD: %d\n", dmabuf_fd);
            ret = GST_FLOW_ERROR;
            goto cleanup;
        }

        // Do ALL processing while sample is still valid
        {
            
#if 1 // Run in async mode, best for stable FPS
            data->mlpp->write_dmabuf_to_mlpp(dmabuf_fd, (int)gst_memory_get_sizes(mem, NULL, NULL));
            data->new_frame_ready = true;
            if (data->video_index == 4 && DEBUG)
            {
                // Print current nanosecond timestamp with delta calculation
                static auto last_timestamp = std::chrono::high_resolution_clock::now();
                auto now = std::chrono::high_resolution_clock::now();
                auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
                auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_timestamp).count();
                g_print("Stream %d Timestamp: %ld nanoseconds (delta: %ld ms)\n", data->video_index, nanoseconds, delta_ms);
                last_timestamp = now;
            }
            
#else // sychnronize the inputs, makes framerate less stable      
            std::lock_guard<std::mutex> lock(*data->frame_ready_mutex);
            // Determine if this is FFC stream (video_index 4) or SVC stream (video_index 0-3)
            bool is_ffc_stream = (data->video_index == 4);
            
            if (is_ffc_stream) {
                // FFC stream: Wait until all 4 SVC streams have written their frames
                //std::lock_guard<std::mutex> sync_lock(sync_mutex);
                ffc_frame_ready.store(1);

                // Process the FFC frame with lastest SVC data
                // SVC data must be written before FFC, only update DMABUF for SVC is if changed, since its 15FPS its not always changing
                if(svc_frame_count[0].load() > svc_frame_count_last[0].load()) {data->mlpp_svc0->write_dmabuf_to_mlpp(dmafd_svc[0], dmafd_svc_size[0]); }
                if(svc_frame_count[1].load() > svc_frame_count_last[1].load()) {data->mlpp_svc1->write_dmabuf_to_mlpp(dmafd_svc[1], dmafd_svc_size[1]); }
                if(svc_frame_count[2].load() > svc_frame_count_last[2].load()) {data->mlpp_svc2->write_dmabuf_to_mlpp(dmafd_svc[2], dmafd_svc_size[2]); }
                if(svc_frame_count[3].load() > svc_frame_count_last[3].load()) {data->mlpp_svc3->write_dmabuf_to_mlpp(dmafd_svc[3], dmafd_svc_size[3]); }

                if (DEBUG)
                {
                    // Print current nanosecond timestamp with delta calculation
                    static auto last_timestamp = std::chrono::high_resolution_clock::now();
                    auto now = std::chrono::high_resolution_clock::now();
                    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
                    auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_timestamp).count();
                    g_print("Stream %d Timestamp: %ld nanoseconds (delta: %ld ms)\n", data->video_index, nanoseconds, delta_ms);
                    last_timestamp = now;
                }

                
                data->mlpp->write_dmabuf_to_mlpp(dmabuf_fd, (int)gst_memory_get_sizes(mem, NULL, NULL));
                // Set flag to indicate new frame is ready for processing
                data->new_frame_ready = true;
                for ( int i = 0; i<4; i++ )
                    svc_frame_count_last[i].store(svc_frame_count[i]);
                
                
            } 
            else { 
                // Save DMABUF of SVC camera and then write it with FFC
                dmafd_svc[data->video_index] = dmabuf_fd;
                dmafd_svc_size[data->video_index] = (int)gst_memory_get_sizes(mem, NULL, NULL);
                svc_frame_count[data->video_index].fetch_add(1);
                
                // Set flag to indicate new frame is ready for processing
                data->new_frame_ready = true;

            }
#endif
        }
        
    } else {
        // Handle non-DMABUF memory (fallback)
        static bool fallback_logged = false;
        if (!fallback_logged) {
            g_print("Warning: Not using DMABUF memory, falling back to memory mapping\n");
            if (mem) {
                g_print("  Memory type: %s\n", G_OBJECT_TYPE_NAME(mem->allocator));
            } else {
                g_print("  No memory found in buffer\n");
            }
            fallback_logged = true;
        }
        
        // For non-DMABUF, we could implement CPU memory fallback here
        // This would involve gst_buffer_map() and copying data
        g_print("Skipping non-DMABUF frame\n");
    }

cleanup:
    // CRITICAL: Always unref the sample to free CMA memory
    if (sample) {
        gst_sample_unref(sample);
    }
    
    return ret;
}

void processImage() {
    int count[5];
    for (int i =0; i<5; i++)
        count[i] = 0;

    setup_signal_handlers(); // Re-enable signal handlers in processImage

    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160, 3);

    MlppHelper mlpp_svc0_in("/dev/mlpp_svc0_in");
    MlppHelper mlpp_svc1_in("/dev/mlpp_svc1_in");
    MlppHelper mlpp_svc2_in("/dev/mlpp_svc2_in");
    MlppHelper mlpp_svc3_in("/dev/mlpp_svc3_in");
    
    //DMA BUF SIZE actually ends up being 2048x1536 instead of 1920x1536
    mlpp_svc0_in.update_mlpp_input_params( 2048, 1536, 3);
    mlpp_svc1_in.update_mlpp_input_params( 2048, 1536, 3);
    mlpp_svc2_in.update_mlpp_input_params( 2048, 1536, 3);
    mlpp_svc3_in.update_mlpp_input_params( 2048, 1536, 3);

    MlppHelper mlpp_svc0_display("/dev/mlpp_svc0_display");
    MlppHelper mlpp_svc1_display("/dev/mlpp_svc1_display");
    MlppHelper mlpp_svc2_display("/dev/mlpp_svc2_display");
    MlppHelper mlpp_svc3_display("/dev/mlpp_svc3_display");
    mlpp_svc0_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc1_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc2_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);
    mlpp_svc3_display.update_mlpp_output_params(640, 512, 0, 1920, 0, 1536, 0, 0, 0, 0.0, 0, 0);


    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");
    mlpp_ffc_display.update_mlpp_output_params( 960, 540, 0, 3840, 0, 2160, 0, 0, 0, 0.0, 0, 0 ); // make YUV output and scale to 960x540    

    int scalex, scaley, width_in, height_in;
    scalex=1920;
    scaley=1080;
    width_in=3840;
    height_in=2160;

    cout << "MLPP Initialized " << endl;

    /// Crop and scale parameters end
    DrmHelper drmdisplayoutput; // drm_helper, setups DRM buffer
    VcuHelper vcu_helper; // Single VcuHelper for multi-pipeline setup
    
    // Create tiled output buffer and mutex
    cv::Mat tiled_output(scaley, scalex, CV_8UC3);
    std::mutex frame_ready_mutex;
    std::mutex svc_input_mutex;


    // Create single multi-stream GStreamer pipeline
    std::vector<std::string> video_filenames = {
        "/root/video/KOR_SVC1_h264_transcode.mp4",   // SVC0 stream (index 0)
        "/root/video/KOR_SVC2_h264_transcode.mp4",   // SVC1 stream (index 1)
        "/root/video/KOR_SVC3_h264_transcode.mp4",   // SVC2 stream (index 2)
        "/root/video/KOR_SVC4_h264_transcode.mp4",   // SVC3 stream (index 3)
        "/root/video/bbb_sunflower_2160p_30fps_normal.mp4"  // FFC stream (index 4)
    };
    
    std::vector<float> video_framerates = {
        15.0f,  // SVC0 framerate
        15.0f,  // SVC1 framerate 
        15.0f,  // SVC2 framerate
        15.0f,  // SVC3 framerate
        30.0f   // FFC framerate
    };
    
    std::vector<GstData*> gst_data_list;
    vcu_helper.setup_multi_appsink_pipeline(video_filenames, video_framerates, new_sample_callback, gst_data_list);
    
    // Assign pointers to individual GstData structures for compatibility
    if (gst_data_list.size() >= 5) {
        // Update MLPP pointers and video_index for each GstData in the list
        gst_data_list[0]->mlpp = &mlpp_svc0_in;
        gst_data_list[0]->width = 2048;
        gst_data_list[0]->height = 1536;
        gst_data_list[0]->video_index = 0;  // SVC0
        gst_data_list[0]->frame_ready_mutex = &svc_input_mutex;
        
        gst_data_list[1]->mlpp = &mlpp_svc1_in;
        gst_data_list[1]->width = 2048;
        gst_data_list[1]->height = 1536;
        gst_data_list[1]->video_index = 1;  // SVC1
        gst_data_list[1]->frame_ready_mutex = &svc_input_mutex;
        
        gst_data_list[2]->mlpp = &mlpp_svc2_in;
        gst_data_list[2]->width = 2048;
        gst_data_list[2]->height = 1536;
        gst_data_list[2]->video_index = 2;  // SVC2
        gst_data_list[2]->frame_ready_mutex = &svc_input_mutex;
        
        gst_data_list[3]->mlpp = &mlpp_svc3_in;
        gst_data_list[3]->width = 2048;
        gst_data_list[3]->height = 1536;
        gst_data_list[3]->video_index = 3;  // SVC3
        gst_data_list[3]->frame_ready_mutex = &svc_input_mutex;
        
        gst_data_list[4]->mlpp = &mlpp_ffc_in;
        gst_data_list[4]->mlpp_svc0 = &mlpp_svc0_in;
        gst_data_list[4]->mlpp_svc1 = &mlpp_svc1_in;
        gst_data_list[4]->mlpp_svc2 = &mlpp_svc2_in;
        gst_data_list[4]->mlpp_svc3 = &mlpp_svc3_in;
        gst_data_list[4]->width = width_in;
        gst_data_list[4]->height = height_in;
        gst_data_list[4]->video_index = 4;  // FFC
        gst_data_list[4]->frame_ready_mutex = &svc_input_mutex;
        
    } else {
        g_printerr("Failed to create sufficient pipeline streams\n");
        return;
    }
    

    cout << "Gstreamer config done, waiting for PLAYING state..." << endl;
    
    // Validate that multi-pipeline was created successfully
    if (gst_data_list.empty() || gst_data_list.size() < 5) {
        g_printerr("Failed to create multi-stream pipeline with sufficient streams\n");
        return;
    }
    
    g_print("Multi-stream pipeline created successfully with %zu streams\n", gst_data_list.size());
    
    
    // Wait for state change to complete with timeout
    GstState state;
    int ret;
    ret = gst_element_get_state(gst_data_list[0]->pipeline, &state, NULL, 5 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to go to PLAYING state.\n");
        
        // Get error message if available
        GstMessage *msg = gst_bus_timed_pop_filtered(gst_data_list[0]->bus, 0, GST_MESSAGE_ERROR);
        if (msg != NULL) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error: %s\n", err->message);
            g_printerr("Debug: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);
        }
        
        // Cleanup will be handled by the multi-pipeline cleanup
        return;
    }
    
    cout << "Pipeline is now STARTING" << endl;
    sleep(1);
    

    // Start non-blocking display thread
    std::thread display_thread(displayThread, &drmdisplayoutput);

    auto start_time = high_resolution_clock::now();
    bool pipeline_running = true;
    int eof_check_counter = 0; // Counter for periodic EOF checking
    cv::Mat RGB_display_output(1080, 1920, CV_8UC3);
    
    while (pipeline_running && !shutdown_requested)
    {
        // Check for EOF - either from any individual stream or the pipeline
        if (any_stream_eof.load()) {
            g_print("EOF detected on at least one stream, stopping all streams...\n");
            pipeline_running = false;
            break;
        }
        
        // Also check pipeline-level EOF periodically as backup
        if (++eof_check_counter >= 100) {
            eof_check_counter = 0;
            if(vcu_helper.check_eof(gst_data_list[0])) {
                g_print("EOF detected on multi-stream pipeline, stopping...\n");
                pipeline_running = false;
                break;
            }
        }

        // Check for shutdown request more frequently
        if (shutdown_requested) {
            g_print("Shutdown requested, stopping pipeline...\n");
            break;
        }
        
        // Wait for new frame from GStreamer (native API with DMABUF access)
        bool frame_available = false;
        
        cv::Mat ffc_output(540, 960, CV_8UC3);
        cv::Mat svc0_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
        cv::Mat svc1_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
        cv::Mat svc2_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
        cv::Mat svc3_output(512, 640, CV_8UC3, cv::Scalar(128, 128, 128));
        
        if (gst_data_list[4]->new_frame_ready) {
            mlpp_ffc_display.read_image_from_mlpp(&ffc_output);
            cv::Rect top_left_roi(0, 0, 960, 540); // x, y, width, height
            ffc_output.copyTo(RGB_display_output(top_left_roi));
            gst_data_list[4]->new_frame_ready = false;
            frame_available = true;
            count[0]++;


            if (gst_data_list[0]->new_frame_ready) {
                mlpp_svc0_display.read_image_from_mlpp(&svc0_output);
                cv::Rect roi_svc0(960, 0, svc0_output.cols, svc0_output.rows);
                svc0_output.copyTo(RGB_display_output(roi_svc0));
                gst_data_list[0]->new_frame_ready = false;
                frame_available = true;
                count[1]++;
            }
            if (gst_data_list[1]->new_frame_ready) {
                mlpp_svc1_display.read_image_from_mlpp(&svc1_output);
                cv::Rect roi_svc1(0, 540, svc1_output.cols, svc1_output.rows);
                svc1_output.copyTo(RGB_display_output(roi_svc1));
                gst_data_list[1]->new_frame_ready = false;
                frame_available = true;
                count[2]++;
            }
            if (gst_data_list[2]->new_frame_ready) {
                mlpp_svc2_display.read_image_from_mlpp(&svc2_output);
                cv::Rect roi_svc2(640, 540, svc2_output.cols, svc2_output.rows);
                svc2_output.copyTo(RGB_display_output(roi_svc2));
                gst_data_list[2]->new_frame_ready = false;
                frame_available = true;
                count[3]++;
            }
            if (gst_data_list[3]->new_frame_ready) {
                mlpp_svc3_display.read_image_from_mlpp(&svc3_output);
                cv::Rect roi_svc3(1280, 540, svc3_output.cols, svc3_output.rows);
                svc3_output.copyTo(RGB_display_output(roi_svc3));
                gst_data_list[3]->new_frame_ready = false;
                frame_available = true;
                count[4]++;
            }
        }

        
        if (!frame_available) {
            usleep(1000); // Wait 1ms if no frame available
            continue;
        }
        else {
            // Queue the tiled output for display
            queueFrameForDisplay(RGB_display_output); 
        }

        if (count[0] > 99) // Calculate FPS less frequently
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps_ffc = static_cast<double>(count[0]) / (elapsed_time.count() / 1000.0);
            double fps_svc0 = static_cast<double>(count[1]) / (elapsed_time.count() / 1000.0);
            double fps_svc1 = static_cast<double>(count[2]) / (elapsed_time.count() / 1000.0);
            double fps_svc2 = static_cast<double>(count[3]) / (elapsed_time.count() / 1000.0);
            double fps_svc3 = static_cast<double>(count[4]) / (elapsed_time.count() / 1000.0);
            cout << std::fixed << std::setprecision(2) << "FFC FPS: " << fps_ffc 
            << " SVC0 FPS: " << fps_svc0
            << " SVC1 FPS: " << fps_svc1
            << " SVC2 FPS: " << fps_svc2
            << " SVC3 FPS: " << fps_svc3
            << endl;

            for (int i =0; i<5; i++)
                count[i] = 0;
            start_time = high_resolution_clock::now();
        
        }
        
	    
    }
    
    std::cout << "end of file" << std::endl;
    
    // Stop display thread first
    display_running = false;
    shutdown_requested = true;

    sleep(1);
    
    // Clean up multi-stream GStreamer pipeline
    if (!gst_data_list.empty()) {
        vcu_helper.cleanup_multi_pipeline(gst_data_list);
    }
    
    std::cout << "GStreamer pipeline closed." << std::endl;

    
    mlpp_ffc_display.closeDevice();
    mlpp_ffc_in.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;


    // Wait for display thread to finish
    //display_thread.join();

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
        std::cout << "The nv12 video file does not exist. Download it to /root/video/:" << std::endl;
        std::cout << "The video file does not exist. Download it to /root/video/:" << std::endl;
        std::cout << "wget -P /root/video/ https://download.blender.org/demo/movies/BBB/bbb_sunflower_2160p_30fps_normal.mp4.zip" << std::endl;
        std::cout << "unzip /root/video/bbb_sunflower_2160p_30fps_normal.mp4.zip -d /root/video/" << std::endl;
        std::cout << "sync" << std::endl;
        std::cout << "rm /root/video/bbb_sunflower_2160p_30fps_normal.mp4.zip" << std::endl;
        std::cout << "Then run this application again." << std::endl;
        std::cout << std::endl;
        return 1;
    }   
    
    // Initialize GStreamer
    gst_init(NULL, NULL);
    
    // Install signal handlers before doing anything
    setup_signal_handlers();
    
    try {
        processImage();
    } catch (const std::exception& e) {
        g_print("Exception caught: %s\n", e.what());
    }
    
    //g_print("Application terminated gracefully\n");
    return 0;
}

