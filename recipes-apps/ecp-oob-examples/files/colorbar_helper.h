// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC

/**
 * colorbar_helper.h
 * Header file for utility functions generating color bar test patterns.
 * Author: LT Thomas
 */

#ifndef COLORBAR_HELPER_H
#define COLORBAR_HELPER_H

#include <opencv2/opencv.hpp>
#include <vector>

// External declaration of color bar RGB values
extern std::vector<cv::Scalar> rgb_colors;

// Function declarations
void rgb_to_yuv(uchar r, uchar g, uchar b, uchar& y, uchar& u, uchar& v);
void generateColorBarsNV12(cv::Mat& nv12_output_mat, int width_in, int height_in);

#endif // COLORBAR_HELPER_H