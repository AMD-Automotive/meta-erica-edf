// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC
// Maintainer: KC

#include <iostream>
#include <string>
#include <unistd.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>


int main(int argc, char** argv) {

    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <Image_Path>" << std::endl;
        return -1;
    }

    // Enable OpenCL acceleration
    if (!cv::ocl::haveOpenCL()) {
        std::cout << "OpenCL is not available. Exiting." << std::endl;
        return -1;
    }

    cv::ocl::setUseOpenCL(true);


    // Read the image
    cv::Mat original_image = cv::imread(argv[1]);
    // Convert Mat to UMat for GPU processing
    cv::UMat umat_original;
    original_image.copyTo(umat_original);


    // Define the scaling factor and output size
    double scale = 0.5;
    cv::Size new_size(static_cast<int>(umat_original.cols * scale), static_cast<int>(umat_original.rows * scale));


    // Create a UMat for the scaled image
    cv::UMat umat_scaled;
    cv::Mat cv_scaled;

    // Resize the image using UMat. This operation is accelerated by OpenCL.

    auto start = std::chrono::high_resolution_clock::now();
    cv::resize(umat_original, umat_scaled, new_size, 0, 0, cv::INTER_LINEAR);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "OpenCL cv::resize took " << duration_ns << " ns" << std::endl;

    auto start_cpu = std::chrono::high_resolution_clock::now();
    cv::resize(original_image, cv_scaled, new_size, 0, 0, cv::INTER_LINEAR);
    auto end_cpu = std::chrono::high_resolution_clock::now();
    auto duration_cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_cpu - start_cpu).count();
    std::cout << "CPU cv::resize took " << duration_cpu_ns << " ns" << std::endl;

    // Convert the UMat back to Mat for display (if needed)
    cv::Mat scaled_image;
    umat_scaled.copyTo(scaled_image);

    cv::imwrite("scaled_image.png", scaled_image);
    cv::imwrite("original_image.png", original_image);


    return 0;

}