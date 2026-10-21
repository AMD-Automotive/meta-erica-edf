// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Contributors: LT
// Maintainer: LT

/**
 * onnx_yolo8n_helper.h
 *
 * Minimal YOLOv8-nano object-detection helper built on ONNX Runtime with the
 * Vitis AI (VAIML) Execution Provider for the Versal AI Edge Gen 2 (RAVE2) NPU.
 *
 * Designed to plug into the MLPP/DRM pipeline: feed it the 512x512 RGBX frame
 * produced by an MLPP "out" node (e.g. /dev/mlpp_ffc_out0) and it returns
 * COCO-80 detections. A companion helper draws those detections straight onto
 * a packed NV12 display buffer (e.g. the /dev/mlpp_ffc_display frame).
 *
 * The ONNX Runtime headers are kept out of this header (pImpl) so callers only
 * need OpenCV to include it.
 *
 * Author: LT Thomas
 */

#ifndef ONNX_YOLO8N_HELPER_H
#define ONNX_YOLO8N_HELPER_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// A single decoded detection. `box` is xywh (top-left + size) expressed in the
// pixel coordinates of the image that was passed to detect().
struct YoloDetection {
    cv::Rect2f box;
    float      score    = 0.0f;
    int        class_id = 0;
};

// Runtime configuration for the detector.
struct OnnxYolo8nConfig {
    // Model + VitisAI cache (defaults assume the files are staged next to the
    // executable's working directory; override as needed).
    std::string model_path  = "/etc/vai/models/yolo8n_int8_rave2/yolov8n_VINT8_adaround_npu.onnx";
    std::string config_file = "/etc/vai/models/yolo8n_int8_rave2/vitisai_config_rave2.json";
    std::string cache_dir   = "/etc/vai/models/yolo8n_int8_rave2/rave2_compiled_int8";
    std::string cache_key   = "yolov8n_int8";

    bool  use_npu   = true;   // VitisAIExecutionProvider (NPU) vs CPU EP
    bool  normalize = false;  // FLOAT models only: true=[0,1], false=[0,255]

    // INT8 input quantization (compiled *_npu model folds QuantizeLinear into
    // the graph input): int8 = clamp(pixel / quant_scale + quant_zp).
    float quant_scale = 255.0f / 128.0f;  // ~1.9922
    int   quant_zp    = 0;

    float conf_thr = 0.25f;   // confidence threshold
    float iou_thr  = 0.45f;   // NMS IoU threshold
    int   imgsz    = 0;       // network input size (0 => infer from model)

    OnnxYolo8nConfig() = default;

    // Convenience constructor: override the VitisAI cache_key (the compiled
    // model subfolder) when a non-empty string is given; otherwise keep the
    // default. Useful when several detector instances each need a distinct
    // cache so they don't share/clobber the same compiled artifacts.
    explicit OnnxYolo8nConfig(const std::string& cache_key_override) {
        if (!cache_key_override.empty()) cache_key = cache_key_override;
    }

    // Pin this detector to a specific compiled cache location by overriding
    // both cache_dir and cache_key (non-empty values only). This is how you
    // select an NPU partition: each partition is precompiled into its own
    // directory (e.g. rave2_compiled_int8/yolov8n_int8_p0) whose .rai lives in
    // the "yolov8n_int8" subfolder, so pass:
    //   OnnxYolo8nConfig("rave2_compiled_int8/yolov8n_int8_p0", "yolov8n_int8")
    OnnxYolo8nConfig(const std::string& cache_dir_override,
                     const std::string& cache_key_override) {
        if (!cache_dir_override.empty()) cache_dir = cache_dir_override;
        if (!cache_key_override.empty()) cache_key = cache_key_override;
    }

    // Select one of the precompiled NPU partitions (0,1,2,...). Builds the
    // cache_dir as "<base>/<cache_key>_p<idx>" and keeps cache_key unchanged,
    // matching the staged partition layout
    // (rave2_compiled_int8/yolov8n_int8_pN/yolov8n_int8/yolov8n_int8.rai).
    static OnnxYolo8nConfig partition(int idx,
                                      const std::string& base = "rave2_compiled_int8",
                                      const std::string& key  = "yolov8n_int8") {
        OnnxYolo8nConfig c;
        c.cache_key = key;
        c.cache_dir = base + "/" + key + "_p" + std::to_string(idx);
        return c;
    }
};

class OnnxYolo8nHelper {
public:
    explicit OnnxYolo8nHelper(const OnnxYolo8nConfig& cfg = OnnxYolo8nConfig());
    ~OnnxYolo8nHelper();

    OnnxYolo8nHelper(const OnnxYolo8nHelper&)            = delete;
    OnnxYolo8nHelper& operator=(const OnnxYolo8nHelper&) = delete;

    // Load the model / build the NPU session. Returns false on failure.
    bool init();

    // Square network input size (inferred from the model unless overridden).
    int input_size() const;

    // Run inference on one frame.
    //   * CV_8UC4 input is treated as MLPP RGBX (channels R,G,B,X).
    //   * CV_8UC3 input is treated as OpenCV BGR.
    // Detections are returned in the passed image's pixel coordinates.
    std::vector<YoloDetection> detect(const cv::Mat& image);

    // COCO-80 class name for an id ("obj" if out of range).
    static const char* class_name(int class_id);

    // Draw detections onto a packed NV12 buffer (CV_8UC1, (H*3/2) rows x W cols,
    // Y plane followed by interleaved UV). `dets` are given in src_w x src_h
    // coordinates (the detect() input size) and are scaled to the NV12 size.
    static void drawDetectionsNV12(cv::Mat& nv12, int nv12_w, int nv12_h,
                                   const std::vector<YoloDetection>& dets,
                                   int src_w, int src_h);

private:
    struct Impl;
    Impl* impl_;
};

#endif  // ONNX_YOLO8N_HELPER_H
