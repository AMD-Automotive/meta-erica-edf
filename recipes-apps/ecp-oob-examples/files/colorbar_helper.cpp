// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC

/**
 * colorbar_helper.cpp
 * Utility functions for generating color bar test patterns.
 * Author: LT Thomas
 */

#include "colorbar_helper.h"

// Define color bar RGB values
std::vector<cv::Scalar> rgb_colors = {
	cv::Scalar(255, 255, 255), // White
	cv::Scalar(0, 255, 255),   // Yellow
	cv::Scalar(255, 255, 0),   // Cyan (adjusted for BGR order)
	cv::Scalar(0, 255, 0),     // Green
	cv::Scalar(255, 0, 255),   // Magenta (adjusted for BGR order)
	cv::Scalar(0, 0, 255),     // Red
	cv::Scalar(255, 0, 0),     // Blue
	cv::Scalar(0, 0, 0)        // Black
};

// Function to convert RGB to Y, U, V
// (Simplified for full range YCbCr, not strictly BT.601/709 limits)
void rgb_to_yuv(uchar r, uchar g, uchar b, uchar& y, uchar& u, uchar& v) {
    // ITU-R BT.601 conversion (approximate for 0-255 range)
    // Note: Official YCbCr has limited range (Y: 16-235, Cb/Cr: 16-240),
    // but for simplicity and direct filling, we'll use full 0-255.
    y = static_cast<uchar>(0.299 * r + 0.587 * g + 0.114 * b);
    u = static_cast<uchar>( -0.169 * r - 0.331 * g + 0.500 * b + 128);
    v = static_cast<uchar>( 0.500 * r - 0.419 * g - 0.081 * b + 128);
}

void generateColorBarsNV12(cv::Mat& nv12_output_mat, int width_in, int height_in) {
    // Create color bars in NV12 format
    int bar_width = width_in / rgb_colors.size();

    // --- Fill Y Plane ---
    cv::Mat y_plane = nv12_output_mat(cv::Rect(0, 0, width_in, height_in));
    for (int i = 0; i < rgb_colors.size(); ++i) {
        uchar y_val, u_val, v_val;
        rgb_to_yuv(rgb_colors[i][2], rgb_colors[i][1], rgb_colors[i][0], y_val, u_val, v_val); // RGB order for rgb_to_yuv

        // Fill the current bar segment in the Y plane
        y_plane.colRange(i * bar_width, (i + 1) * bar_width).setTo(cv::Scalar(y_val));
    }

    // --- Fill UV Plane ---
    // The UV plane starts immediately after the Y plane
    cv::Mat uv_plane = nv12_output_mat(cv::Rect(0, height_in, width_in, height_in / 2));

    for (int i = 0; i < rgb_colors.size(); ++i) {
        uchar y_val, u_val, v_val;
        rgb_to_yuv(rgb_colors[i][2], rgb_colors[i][1], rgb_colors[i][0], y_val, u_val, v_val); // RGB order for rgb_to_yuv
        for (int x = i * bar_width; x < (i + 1) * bar_width; x += 2) {
            uv_plane.col(x).setTo(cv::Scalar(u_val));   // U component
            if (x + 1 < (i + 1) * bar_width) {
                uv_plane.col(x + 1).setTo(cv::Scalar(v_val)); // V component
            }
        }
    }
}