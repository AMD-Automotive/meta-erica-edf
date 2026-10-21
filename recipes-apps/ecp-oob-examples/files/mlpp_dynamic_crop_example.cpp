// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC


/**
 * mlpp_dynamic_crop_example.cpp
 * Example application demonstrating dynamic crop parameter changes during MLPP processing.
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
    mlpp_ffc_in.update_mlpp_input_params( 3840, 2160);

    MlppHelper mlpp_ffc_display("/dev/mlpp_ffc_display");

    ml_pp_output_params mlpp_out_params;
    memset(&mlpp_out_params, 0, sizeof(mlpp_out_params));
    mlpp_out_params.width_out   = 1920;
    mlpp_out_params.height_out  = 1080;
    mlpp_out_params.crop_xstart = 0;
    mlpp_out_params.crop_xstop  = 3840;
    mlpp_out_params.crop_ystart = 0;
    mlpp_out_params.crop_ystop  = 2160;
    mlpp_out_params.sub_gy      = 255; // bypass meansubsum 
    mlpp_out_params.sub_bcb     = 255; // bypass meansubsum
    mlpp_out_params.sub_rcr     = 255; // bypass meansubsum
    mlpp_out_params.scale_y     = 1;
    mlpp_out_params.scale_cb    = 1;
    mlpp_out_params.scale_cr    = 1;
    mlpp_out_params.color_from_csc = 1; // 0=RGB conversion, 1=YUV conversion
    mlpp_out_params.color_out   = 4; // 0|1|3=Unsupported, 2=RGBX/YUVX (packed), 4=NV12 (display only)
    mlpp_ffc_display.update_mlpp_params(mlpp_out_params);


    DrmHelper displayoutput(1920, 1080, 60, DRM_OUT_NV12);

    int width_in = 3840;
    int height_in = 2160;
    cv::Mat nv12_output_mat(height_in + height_in / 2, width_in, CV_8UC1, cv::Scalar(0));
    generateColorBarsNV12(nv12_output_mat, width_in, height_in);
    
    
    if (nv12_output_mat.isContinuous()) {
        std::cout << "Mat is continuous. Writing entire data block in one go." << std::endl;
    } 

    

    int cropx = 3840;
    int cropy = 2160;
    auto start_time = high_resolution_clock::now();

    cv::Mat ffc_output_nv12(1080 + 1080/2, 1920, CV_8UC1, cv::Scalar(0)); // NV12


    while (1) 
    {
        auto frame_start = high_resolution_clock::now();

        //Make an output that has a text counting on it
        cv::Mat nv12_output_w_counter = nv12_output_mat.clone();
        std::string customText = std::to_string(count);
        cv::putText(nv12_output_w_counter, customText, cv::Point(100, 200), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(0, 0, 0), 2, cv::LINE_8);

        // Update crop parameters   
        mlpp_out_params.crop_xstart = 0;
        mlpp_out_params.crop_xstop  = cropx;
        mlpp_out_params.crop_ystart = 0;
        mlpp_out_params.crop_ystop  = cropy;


        mlpp_ffc_display.update_mlpp_params(mlpp_out_params);


        // Write NV12 image into MLPP pipe
        // If the pipeline is still running then it will block the start of the next cycle
        mlpp_ffc_in.write_image_to_mlpp(&nv12_output_w_counter);


        // Read RGB output from MLPP pipe when it is ready, this call blocks until the output is ready
        mlpp_ffc_display.read_image_from_mlpp(&ffc_output_nv12);


        // Write it to display
        displayoutput.DrmWriteOutputNV12VSync(&ffc_output_nv12);

        cropx = cropx-4;
        cropy = cropy-4;
        
 
        if (count > 200) 
        {
            auto elapsed_time = duration_cast<milliseconds>(high_resolution_clock::now() - start_time);
            double fps = static_cast<double>(count) / (elapsed_time.count() / 1000.0);
            cout << std::fixed << std::setprecision(2) << "FPS: " << fps << endl;
            count = 0;
            start_time = high_resolution_clock::now();

            // Put defaults back
            cropx = 3840;
            cropy = 2160;
            
        }
        count++;
	    
    }
    // Cleanup
    mlpp_ffc_display.closeDevice();
    std::cout << "ML preprocessing device closed." << std::endl;
    
    return;
}


int main() {

    processImage();

    return 0;
}
