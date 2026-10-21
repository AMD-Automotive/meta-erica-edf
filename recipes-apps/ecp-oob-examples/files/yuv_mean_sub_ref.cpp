// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: KC

// yuv_mean_sub_ref.cpp
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include "drm_helper_yuv444m.h"
#include "mlpp_helper.h"

template<typename T>
static inline int32_t (FLT_ROUND)(T const x) noexcept {
    return static_cast<int32_t>((x > 0.0F) ? std::floor(x + 0.500005F)
                                           : std::ceil (x - 0.499995F));
}

template<typename T, typename R = int32_t>
static inline R(INT_SATURATION)(T const x, int32_t const n) noexcept {
    const R minv = -(R(1) << (n - 1));
    const R maxv =  (R(1) << (n - 1)) - 1;
    return (x > maxv) ? maxv : (x < minv) ? minv : R(x);
}





static bool read_all(const std::string& path, std::vector<uint8_t>& buf) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    ifs.seekg(0, std::ios::end);
    const std::streamsize sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    buf.resize(static_cast<size_t>(sz));
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(buf.data()), sz));
}

static bool write_all(const std::string& path, const std::vector<int8_t>& buf) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return static_cast<bool>(ofs);
}

void overlay_yuv444_data(int x_offset, int y_offset, cv::Mat& frame, cv::Mat& displayoutput) {

    // Copy Y plane
    int width = frame.cols;
    int height = frame.rows / 3;
    //cout << "widthe height" <<width << " " << height << "\n" <<endl;
    //sleep(1);
    cv::Rect src_y(0, 0, width, height);
    cv::Rect dst_y(x_offset, y_offset, width, height);
    frame(src_y).copyTo(displayoutput(dst_y));
    // Copy U plane
    cv::Rect src_u(0, height, width, height);
    cv::Rect dst_u(x_offset, 1080 + y_offset, width, height);
    frame(src_u).copyTo(displayoutput(dst_u));
    // Copy V plane
    cv::Rect src_v(0, height*2, width, height);
    cv::Rect dst_v(x_offset, 2160 + y_offset, width, height);
    frame(src_v).copyTo(displayoutput(dst_v));
}

struct QParams {
    float sY, sU, sV;       // scale (>0)
    int   zpY, zpU, zpV;    // zero_point (usually in [-128,127])
};

static inline int8_t quantize_i8(uint8_t x_u8, int mean_i, float inv_s, int zp) noexcept {
    // r = (x - mean); q = round(r / s) + zp; clamp to int8
    const float r   = static_cast<float>(static_cast<int32_t>(x_u8) - mean_i);
    const int   qi  = FLT_ROUND(r * inv_s) + zp;
    const int   sat = INT_SATURATION<int, int>(qi, 8);
    return static_cast<int8_t>(sat);
}

int main(int argc, char** argv) {
    /*if (argc != 8) {
        std::cerr << "Usage: " << argv[0]
                  << " <in_444p.yuv> <width> <height> <meanY> <meanU> <meanV> <out.bin>\n";
        std::cerr << "Input format: planar YUV444p 8-bit, planes = [Y][U][V]\n";
        return 1;
    }*/

    // Args:
    // Common s/zp:  10 args
    //   0:prog 1:in 2:W 3:H 4:meanY 5:meanU 6:meanV 7:scale 8:zp 9:out
    // Per-channel: 14 args
    //   ... 7:sY 8:zpY 9:sU 10:zpU 11:sV 12:zpV 13:out
    if (!(argc == 10 || argc == 14)) {
        std::cerr <<
        "Usage (common s/zp):\n  " << argv[0]
        << " <in_444p.yuv> <W> <H> <meanY> <meanU> <meanV> <scale> <zero_point> <out.bin>\n"
        "Usage (per-channel s/zp):\n  " << argv[0]
        << " <in_444p.yuv> <W> <H> <meanY> <meanU> <meanV> <sY> <zpY> <sU> <zpU> <sV> <zpV> <out.bin>\n"
        "Input format: planar YUV444p 8-bit, planes=[Y][U][V]\n";
        return 1;
    }


    const std::string in_path = argv[1];
    const int W = std::atoi(argv[2]);
    const int H = std::atoi(argv[3]);
    const float meanY_f = std::atof(argv[4]);
    const float meanU_f = std::atof(argv[5]);
    const float meanV_f = std::atof(argv[6]);
    //const std::string out_path = argv[7];

    if (W <= 0 || H <= 0) {
        std::cerr << "Invalid W/H\n";
        return 1;
    }

    // Round means to integer with the exact rule
    const int meanY = INT_SATURATION(FLT_ROUND(meanY_f), 9);
    const int meanU = INT_SATURATION(FLT_ROUND(meanU_f), 9);
    const int meanV = INT_SATURATION(FLT_ROUND(meanV_f), 9);

    QParams qp{};
    std::string out_path;
    if (argc == 10) {
        const float s = std::atof(argv[7]);
        const int   zp = std::atoi(argv[8]);
        out_path = argv[9];
        if (!(s > 0.f)) { std::cerr << "scale must be > 0\n"; return 1; }
        qp = {s, s, s, zp, zp, zp};
    } else { // argc == 14
        qp.sY = std::atof(argv[7]);   qp.zpY = std::atoi(argv[8]);
        qp.sU = std::atof(argv[9]);   qp.zpU = std::atoi(argv[10]);
        qp.sV = std::atof(argv[11]);  qp.zpV = std::atoi(argv[12]);
        out_path = argv[13];
        if (!(qp.sY>0.f && qp.sU>0.f && qp.sV>0.f)) {
            std::cerr << "All scales must be > 0\n"; return 1;
        }
    }

    std::vector<uint8_t> inbuf;
    if (!read_all(in_path, inbuf)) {
        std::cerr << "Failed to read: " << in_path << "\n";
        return 1;
    }

    const size_t plane = static_cast<size_t>(W) * static_cast<size_t>(H);
    const size_t expected = plane * 3;
    if (inbuf.size() != expected) {
        std::cerr << "Size mismatch: got " << inbuf.size()
                  << " bytes, expected " << expected << " (W*H*3)\n";
        return 1;
    }

    const uint8_t* Yp = inbuf.data();
    const uint8_t* Up = inbuf.data() + plane;
    const uint8_t* Vp = inbuf.data() + plane * 2;

    std::vector<int8_t> outbuf(expected); // planar int8: [Y][U][V]
    int8_t* Yo = outbuf.data();
    int8_t* Uo = outbuf.data() + plane;
    int8_t* Vo = outbuf.data() + plane * 2;

    // Precompute 1/scale
    const float inv_sY = 1.0f / qp.sY;
    const float inv_sU = 1.0f / qp.sU;
    const float inv_sV = 1.0f / qp.sV;

    // Quantize: q = round((x - mean)/s) + zp ; clamp to [-128,127]
    for (size_t i = 0; i < plane; ++i) {
        Yo[i] = quantize_i8(Yp[i], meanY, inv_sY, qp.zpY);
        Uo[i] = quantize_i8(Up[i], meanU, inv_sU, qp.zpU);
        Vo[i] = quantize_i8(Vp[i], meanV, inv_sV, qp.zpV);
    }

    if (!write_all(out_path, outbuf)) {
        std::cerr << "Failed to write: " << out_path << "\n";
        return 1;
    }

        std::cout << "Done. W=" << W << " H=" << H
              << " mean(Y,U,V)=(" << meanY << "," << meanU << "," << meanV << ")"
              << " scales(Y,U,V)=(" << qp.sY << "," << qp.sU << "," << qp.sV << ")"
              << " zp(Y,U,V)=(" << qp.zpY << "," << qp.zpU << "," << qp.zpV << ")"
              << " -> " << out_path << "\n";


    DrmHelperYUV444 displayoutput;
    cv::Mat display_frame(1080*3, 2048, CV_8UC1, cv::Scalar(16)); 

    // Input image
    cv::Mat input_yuv444(H*3, W, CV_8UC1);
    memcpy(input_yuv444.data, inbuf.data(), expected);   
    overlay_yuv444_data(0, 0, input_yuv444, display_frame);

    //Golden Image
    cv::Mat golden_yuv444(H*3, W, CV_8UC1);
    memcpy(golden_yuv444.data, outbuf.data(), expected);
    overlay_yuv444_data(0, H, golden_yuv444, display_frame);


    // MLPP Output Image
    cv::Mat mlpp_yuv444(H*3, W, CV_8UC1);
 
    MlppHelper mlpp_ffc_in; 
    mlpp_ffc_in.openDevice("/dev/mlpp_ffc_in");


    ml_pp_input_params mlpp_in_params;
    memset(&mlpp_in_params, 0, sizeof(mlpp_in_params));
    mlpp_in_params.width_in    = W;
    mlpp_in_params.height_in   = H;
    mlpp_in_params.color_in    = 1; // 0 - RGB/YUV444P , 1 - YUV444M, 2 - UYVY 4:2:2, 3 - NV12
    mlpp_ffc_in.update_mlpp_params(mlpp_in_params);

    MlppHelper mlpp_ffc_out0("/dev/mlpp_ffc_out0");
    MlppHelper mlpp_ffc_out1("/dev/mlpp_ffc_out1");
    MlppHelper mlpp_ffc_out2("/dev/mlpp_ffc_out2");
    MlppHelper mlpp_ffc_out3("/dev/mlpp_ffc_out3");
    MlppHelper mlpp_ffc_out4("/dev/mlpp_ffc_out4");
    mlpp_ffc_out0.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0, 1, 1 );
    mlpp_ffc_out1.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0, 1, 1 );
    mlpp_ffc_out2.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0, 1, 1 );
    mlpp_ffc_out3.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0, 1, 1 );
    mlpp_ffc_out4.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0, 1, 1 );
        

    MlppHelper mlpp_ffc_out5("/dev/mlpp_ffc_out5");
    mlpp_ffc_out5.update_mlpp_output_params( W, H, 0, W, 0, H, meanY, meanU, meanV, 1/(std::atof(argv[7])), 1, 1 );

    MlppHelper mlpp_ffc_display;
    mlpp_ffc_display.openDevice("/dev/mlpp_ffc_display");
    mlpp_ffc_display.update_mlpp_output_params( W, H, 0, W, 0, H, 0, 0, 0, 0.0, 1, 1 );
    

    mlpp_ffc_in.write_image_to_mlpp(&input_yuv444);
    usleep(15000);
    mlpp_ffc_out5.read_image_from_mlpp(&mlpp_yuv444);


    overlay_yuv444_data(W, H, mlpp_yuv444, display_frame);

    // Calculate average absolute error between golden_yuv444 and mlpp_yuv444
    double total_error = 0.0;
    size_t error_count = 0;
    for (int i = 0; i < golden_yuv444.rows; ++i) {
        const uint8_t* golden_ptr = golden_yuv444.ptr<uint8_t>(i);
        const uint8_t* mlpp_ptr = mlpp_yuv444.ptr<uint8_t>(i);
        for (int j = 0; j < golden_yuv444.cols; ++j) {
            total_error += std::abs(int(golden_ptr[j]) - int(mlpp_ptr[j]));
            ++error_count;
        }
    }
    double avg_error = (error_count > 0) ? (total_error / error_count) : 0.0;
    std::cout << "Average absolute error between golden_yuv444 and mlpp_yuv444: " << avg_error << std::endl;

    cv::putText(display_frame, "Golden", cv::Point(100, H*2+100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(255, 255, 255), 2, cv::LINE_8);
    cv::putText(display_frame, "MLPP", cv::Point(100+W, H*2+100), cv::FONT_HERSHEY_COMPLEX, 3.0, cv::Scalar(255, 255, 255), 2, cv::LINE_8);

    displayoutput.DrmWriteOutputVSync(&display_frame);

    sleep(10);

    return 0;
}
