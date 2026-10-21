// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * drm_helper.h
 * Header file for Direct Rendering Manager (DRM) helper functions and display control.
 * 
 * Supports RGB888 and NV12 outputs formats
 * Can configure for 1920x1080 or 3840x2160
 * Can configure for 30FPS or 60FPS
 * 
 * Author: LT Thomas
 */

#ifndef DRM_HELPER_H
#define DRM_HELPER_H

#include <opencv2/opencv.hpp>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <stdio.h>
#include <libdrm/drm.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>

// Pixel format of the DRM scanout framebuffer.
enum DrmOutputFormat {
    DRM_OUT_RGB888 = 0,  // 24-bit packed RGB (default)
    DRM_OUT_NV12   = 1   // YUV 4:2:0 semi-planar (Y plane + interleaved UV)
};

class DrmHelper {
public:
    // Constructor
    DrmHelper();
    // Configure resolution/refresh and (optionally) the scanout pixel format.
    // Defaults to RGB888; pass DRM_OUT_NV12 for a YUV 4:2:0 semi-planar buffer.
    DrmHelper(uint32_t width, uint32_t height, uint32_t refresh_hz,
              DrmOutputFormat format = DRM_OUT_RGB888);

    // Destructor
    ~DrmHelper();

    // Member functions (accept BGR input; converted to the configured format)
    void DrmWriteOutput( cv::Mat* );
    void DrmWriteOutputVSync( cv::Mat* );
    void DrmWriteOutputFast( cv::Mat* );

    // Write a packed NV12 frame straight to an NV12 framebuffer
    void DrmWriteOutputNV12( cv::Mat* );
    void DrmWriteOutputNV12VSync( cv::Mat* );

    // Overlay an RGBX (CV_8UC4) image onto a packed NV12 Mat at pixel (x, y).
    void overlay_rgbx_data_2_nv12(int x, int y, cv::Mat& rgbx, cv::Mat& nv12_dst);

    // Overlay a YUVX (CV_8UC4, channel order Y, U, V, X) image onto a packed
    // NV12 Mat at pixel (x, y).
    void overlay_yuvx_data_2_nv12(int x, int y, cv::Mat& yuvx, cv::Mat& nv12_dst);

    // Overlay a packed NV12 Mat (CV_8UC1, (h*3/2) rows x w cols) onto another
    // packed NV12 Mat at pixel (x, y).
    void overlay_nv12_data_2_nv12(int x, int y, cv::Mat& nv12_src, cv::Mat& nv12_dst);

private:
    // Copy a packed NV12 Mat (rows = height*3/2) into the NV12 framebuffer,
    // honoring the framebuffer pitch and UV plane offset.
    void blitNv12ToFramebuffer(const cv::Mat& nv12);
    uint8_t * primed_framebuffer;
    struct drm_mode_create_dumb create_request;
    int drm_fd;
    drmModeCrtc * __restrict crtc_to_restore;
    drmModeConnector   * __restrict valid_connector;
    drmModeModeInfo    * __restrict chosen_resolution;
    drmModeEncoder     * __restrict screen_encoder;
    uint32_t frame_buffer_id;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_refresh_hz;
    DrmOutputFormat output_format;
    bool drm_ready;
    
};

#endif // DRM_HELPER_H
