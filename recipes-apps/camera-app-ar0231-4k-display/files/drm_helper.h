// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * drm_helper.h
 * Header file for Direct Rendering Manager (DRM) helper functions and display control.
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

class DrmHelper {
public:
    // Constructor
    DrmHelper();

    // Destructor
    ~DrmHelper();

    // Member functions
    void DrmWriteOutput( cv::Mat* );
    void DrmWriteOutputVSync( cv::Mat* );
    void DrmWriteOutputFast( cv::Mat* );
    cv::Mat getDrmFramebuffer();

private:
    uint8_t * primed_framebuffer;
    struct drm_mode_create_dumb create_request;
    int drm_fd;
    drmModeCrtc * __restrict crtc_to_restore;
    drmModeConnector   * __restrict valid_connector;
    drmModeModeInfo    * __restrict chosen_resolution;
    drmModeEncoder     * __restrict screen_encoder;
    uint32_t frame_buffer_id;
    
};

#endif // DRM_HELPER_H
