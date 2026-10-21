// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * mlpp_helper.h
 * Header file for Machine Learning Pre-Processing (MLPP) hardware interface operations.
 * Author: LT Thomas
 */

#ifndef MLPP_HELPER_H
#define MLPP_HELPER_H

#include <opencv2/opencv.hpp>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>

typedef unsigned short u16;
typedef unsigned char u8;

// ML Preprocessing DMABUF parameters structure
// This structure is used to pass DMABUF parameters to the ML preprocessing pipeline
// It includes the file descriptor, offset, size, and frame sequence number
struct ml_pp_dmabuf_params {
    int dmabuf_fd;          // DMABUF file descriptor from V4L2
    uint32_t offset;        // Offset into the DMABUF
    uint32_t size;          // Size of data in bytes
    uint32_t frame_sequence; // Frame sequence number for synchronization
} __attribute__((packed));

// ML preprocessing output parameters structure
struct ml_pp_output_params {
    /*00*/ u16 width_out;
    /*02*/ u16 height_out;
    /*04*/ u16 crop_xstart;
    /*06*/ u16 crop_xstop;
    /*08*/ u16 crop_ystart;
    /*0A*/ u16 crop_ystop;
    /*0C*/ u8 sub_gy;           // 255 - will bypass Quantization
    /*0D*/ u8 sub_bcb;
    /*0E*/ u8 sub_rcr;
    /*10*/ u16 color_from_csc;	// 0 - RGB , 1 - YUV444M, 2 - BGR
    /*12*/ u16 color_out;		// 2 - RGBX/YUV444(A), 4 - NV12, Options 0,1,3 are deprecated
    /*14*/ int scale_y; 		// Scale of 0 will bypass scale block
    /*18*/ int scale_cb;        // Will round to the nearest power of 2
    /*1C*/ int scale_cr;
} __attribute__((packed));

// ML preprocessing input parameters structure
struct ml_pp_input_params {
    /*00*/ u16 width_in;
    /*04*/ u16 height_in;
    /*08*/ u16 color_in; // 3 - NV12, Options 0,1,2 are deprecated
} __attribute__((packed));

class MlppHelper {
public:
    // Constructor
    MlppHelper();
    MlppHelper(const char* );

    // Destructor
    ~MlppHelper();

    bool openDevice(const char* device_path);
    void closeDevice();
    bool isDeviceOpen() const;

    // Member functions
    void update_mlpp_output_params( u16 widthout, u16 heightout, 
        u16 crop_x_start, u16 crop_x_stop, u16 crop_y_start, u16 crop_y_stop, 
        u8 meany, u8 meanu, u8 mean_v, float scale, u16 csc_out, u16 fmt_out );
    void update_mlpp_input_params( u16 widthin, u16 heightin, u16 fmt_in );
    void update_mlpp_input_params( u16 widthin, u16 heightin ); // Defaults fmt_in to 3 (NV12)
    void update_mlpp_params( struct ml_pp_input_params );
    void update_mlpp_params( struct ml_pp_output_params );
    void read_image_from_mlpp( cv::Mat* );
    int read_image_from_mlpp_dmabuf( void ); // Returns dmabuf_fd
    void write_image_to_mlpp( cv::Mat* );
    void write_dmabuf_to_mlpp( int, int);

private:
    int mlpre_fd;
    int dmabuf_fd;
    
};

#endif // MLPP_HELPER_H
