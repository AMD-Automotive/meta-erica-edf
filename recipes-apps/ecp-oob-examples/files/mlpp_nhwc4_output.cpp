// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * mlpp_nhwc4_output.cpp
 * Example application demonstrating MLPP preprocessing with NHWC4 output format.
 * Author: LT Thomas
 */

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
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

#include "drm_helper.h"
#include "mlpp_helper.h"
#include "colorbar_helper.h"

using namespace std;
using namespace std::chrono;

void processImage() {
    int count = 0;
    
    // mlpp_helper
    MlppHelper mlpp_ffc_in("/dev/mlpp_ffc_in");
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160 ); //NV12

    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    mlpp_ffc_out0.update_mlpp_output_params(608, 256, 0, 3840, 0, 2160, 255, 255, 255, 0.0, 0, 2);

    int width_in = 3840;
    int height_in = 2160;
    cv::Mat nv12_output_mat(height_in + height_in / 2, width_in, CV_8UC1, cv::Scalar(0));

    generateColorBarsNV12(nv12_output_mat, width_in, height_in);
    
    
    if (nv12_output_mat.isContinuous()) {
        std::cout << "Mat is continuous. Writing entire data block in one go." << std::endl;
    } 

    mlpp_ffc_in.write_image_to_mlpp(&nv12_output_mat);


    cv::Mat nhwc_output(256, 608, CV_8UC4);
    mlpp_ffc_out0.read_image_from_mlpp(&nhwc_output);

    // Write nhwc_output to binary file
    std::ofstream outfile("nhwc4_output.bin", std::ios::binary);
    if (outfile.is_open()) {
        outfile.write(reinterpret_cast<const char*>(nhwc_output.data), nhwc_output.total() * nhwc_output.elemSize());
        outfile.close();
        std::cout << "NHWC output written to nhwc_output.bin (" << nhwc_output.total() * nhwc_output.elemSize() << " bytes)" << std::endl;
    } else {
        std::cerr << "Error: Could not open nhwc_output.bin for writing" << std::endl;
    }


    mlpp_ffc_in.closeDevice();
    mlpp_ffc_out0.closeDevice();

    
    return;
}


int main() {

    processImage();

    return 0;
}
