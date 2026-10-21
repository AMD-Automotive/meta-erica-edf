// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC

/**
 * vcu_helper.h
 * Header file for Video Codec Unit (VCU) helper functions and hardware video processing.
 * Author: LT Thomas
 */

// Declare bus_call for external usage
#include <gst/gst.h>
gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
// vcu_helper.h

#ifndef VCU_HELPER_H
#define VCU_HELPER_H

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

// GStreamer includes for native API
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/allocators/gstfdmemory.h>

#include "mlpp_helper.h"


typedef unsigned short u16;
typedef unsigned char u8;
typedef GstFlowReturn (*AppSinkCallback)(GstAppSink *appsink, gpointer user_data);

using namespace std;
using namespace std::chrono;

// GStreamer data structure
typedef struct _GstData {
    GstElement *pipeline;
    GstElement *source;
    GstElement *parser;
    GstElement *decoder;
    GstElement *demux;
    GstElement *videorate;
    GstElement *capsfilter;
    GstElement *appsink;
    GstBus *bus;
    GstAllocator *allocator;
    std::mutex *frame_ready_mutex;
    int width;
    int height;
    bool new_frame_ready;
    int dmabuf_fd; 
    MlppHelper *mlpp;
    MlppHelper *mlpp_svc0;
    MlppHelper *mlpp_svc1;
    MlppHelper *mlpp_svc2;
    MlppHelper *mlpp_svc3;
    int video_index;
} GstData;

class VcuHelper {
public:
    // Constructor
    VcuHelper();

    // Destructor
    ~VcuHelper();

    // Member functions
    void setup_gst_pipeline(const std::string filename, AppSinkCallback new_sample_callback, GstData* gst_data, float desired_video_rate);
    void cleanup_pipeline(GstData* gst_data);
    int check_eof(GstData* gst_data);
    void start_pipeline(GstData* gst_data);
    void setup_multi_appsink_pipeline(const std::vector<std::string>& filenames, 
                                     const std::vector<float>& framerates,
                                     GstFlowReturn (*new_sample_callback)(GstAppSink *appsink, gpointer user_data), 
                                     std::vector<GstData*>& gst_data_list);
    
    void cleanup_multi_pipeline(std::vector<GstData*>& gst_data_list);


private:
    int video_fd[10];
    int video_count;
    
};

#endif // VCU_HELPER_H
