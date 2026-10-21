// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Contributors: LT
// Maintainer: LT

/**
 * onnx_yolo8n_helper.cpp
 *
 * ONNX Runtime + Vitis AI (VAIML) implementation of the YOLOv8-nano detector
 * declared in onnx_yolo8n_helper.h. The ORT session setup, INT8 pre-quantize
 * and YOLOv8 decode/NMS mirror the reference yolov8_nano/cpp/yolov8_npu.cpp;
 * this file additionally renders detections directly onto a packed NV12 buffer.
 *
 * Author: LT Thomas
 */

#include "onnx_yolo8n_helper.h"

// ONNX Runtime C++ API. The target Vitis rootfs installs headers under
// <onnxruntime/core/session/...>; the host release tarball ships them flat.
#if defined(__has_include)
#  if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#    include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#  elif __has_include(<core/session/onnxruntime_cxx_api.h>)
#    include <core/session/onnxruntime_cxx_api.h>
#  else
#    include <onnxruntime_cxx_api.h>
#  endif
#else
#  include <onnxruntime_cxx_api.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <unordered_map>

// ---------------------------------------------------------------------------
// COCO class names (yolov8n is trained on COCO-80).
// ---------------------------------------------------------------------------
namespace {
const char* const kCocoClasses[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
    "truck", "boat", "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag",
    "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
    "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon",
    "bowl", "banana", "apple", "sandwich", "orange", "broccoli", "carrot",
    "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant",
    "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote",
    "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush",
};
constexpr int kNumClasses = 80;

// Deterministic pseudo-random BGR color per class.
cv::Scalar color_for(int class_id) {
    const int h = (class_id * 47) % 180;
    cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(h, 200, 255));
    cv::Mat bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
    const cv::Vec3b c = bgr.at<cv::Vec3b>(0, 0);
    return cv::Scalar(c[0], c[1], c[2]);
}

// BT.601 full-range-ish BGR -> YUV (studio-swing) for NV12 overlays.
void bgr_to_yuv(const cv::Scalar& bgr, int& Y, int& U, int& V) {
    const int B = static_cast<int>(bgr[0]);
    const int G = static_cast<int>(bgr[1]);
    const int R = static_cast<int>(bgr[2]);
    Y = std::clamp((( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16, 0, 255);
    U = std::clamp(((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128, 0, 255);
    V = std::clamp((( 112 * R -  94 * G -  18 * B + 128) >> 8) + 128, 0, 255);
}

// Greedy per-class NMS on xywh boxes.
std::vector<int> nms(const std::vector<cv::Rect2f>& boxes,
                     const std::vector<float>& scores, float iou_thr) {
    std::vector<int> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return scores[a] > scores[b]; });

    std::vector<int>  keep;
    std::vector<char> removed(boxes.size(), 0);
    for (size_t i = 0; i < order.size(); ++i) {
        const int a = order[i];
        if (removed[a]) continue;
        keep.push_back(a);
        const cv::Rect2f& ra = boxes[a];
        const float area_a = ra.width * ra.height;
        for (size_t j = i + 1; j < order.size(); ++j) {
            const int b = order[j];
            if (removed[b]) continue;
            const cv::Rect2f& rb = boxes[b];
            const float xx1 = std::max(ra.x, rb.x);
            const float yy1 = std::max(ra.y, rb.y);
            const float xx2 = std::min(ra.x + ra.width,  rb.x + rb.width);
            const float yy2 = std::min(ra.y + ra.height, rb.y + rb.height);
            const float iw = std::max(0.0f, xx2 - xx1);
            const float ih = std::max(0.0f, yy2 - yy1);
            const float inter = iw * ih;
            const float area_b = rb.width * rb.height;
            const float iou = inter / std::max(area_a + area_b - inter, 1e-6f);
            if (iou > iou_thr) removed[b] = 1;
        }
    }
    return keep;
}
}  // namespace

// ---------------------------------------------------------------------------
// Private implementation (owns the ONNX Runtime session).
// ---------------------------------------------------------------------------
struct OnnxYolo8nHelper::Impl {
    explicit Impl(const OnnxYolo8nConfig& c) : cfg(c) {}

    bool init() {
        // Match the env expected by the VitisAI EP / VAIML cache loader.
        setenv("XLNX_VART_FIRMWARE", "", 1);
        setenv("XLNX_TARGET_NAME", "", 1);
        unsetenv("XLNX_ENABLE_CACHE");  // must NOT be 0 (0 disables cache reuse)

        try {
            env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "onnx_yolo8n");
            Ort::SessionOptions opts;
            int nthreads = 2;
            if (!cfg.use_npu) {
                nthreads = static_cast<int>(std::thread::hardware_concurrency());
                if (nthreads <= 0) nthreads = 4;
            }
            opts.SetIntraOpNumThreads(nthreads);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            if (cfg.use_npu) {
                std::unordered_map<std::string, std::string> vai = {
                    {"config_file", cfg.config_file},
                    {"cache_dir",   cfg.cache_dir},
                    {"cache_key",   cfg.cache_key},
                    {"target",      "VAIML"},
                };
                std::cout << "[yolo] VitisAIExecutionProvider  model="
                          << cfg.model_path << "\n       cache=" << cfg.cache_dir
                          << "/" << cfg.cache_key << std::endl;
                opts.AppendExecutionProvider_VitisAI(vai);
            } else {
                std::cout << "[yolo] CPUExecutionProvider  model="
                          << cfg.model_path << std::endl;
            }

            session = Ort::Session(env, cfg.model_path.c_str(), opts);

            Ort::AllocatorWithDefaultOptions alloc;
            input_name_str  = session.GetInputNameAllocated(0, alloc).get();
            output_name_str = session.GetOutputNameAllocated(0, alloc).get();
            input_name  = input_name_str.c_str();
            output_name = output_name_str.c_str();

            auto in_info       = session.GetInputTypeInfo(0);
            auto in_shape_info = in_info.GetTensorTypeAndShapeInfo();
            input_shape  = in_shape_info.GetShape();          // [1,3,H,W]
            in_elem_type = in_shape_info.GetElementType();

            imgsz = cfg.imgsz;
            if (imgsz <= 0) {
                const int64_t h = input_shape.size() >= 2
                                  ? input_shape[input_shape.size() - 2] : 640;
                const int64_t w = input_shape.size() >= 1
                                  ? input_shape[input_shape.size() - 1] : 640;
                imgsz = static_cast<int>(h > 0 ? h : (w > 0 ? w : 640));
            }
            if (imgsz <= 0) imgsz = 640;

            std::cout << "[yolo] input='" << input_name << "' size=" << imgsz
                      << " dtype="
                      << (in_elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
                              ? "float32"
                          : in_elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8
                              ? "int8" : "other")
                      << std::endl;

            if (in_elem_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
                in_elem_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
                std::cerr << "[yolo][warn] unsupported model input dtype; only "
                             "float32 and int8 inputs are handled." << std::endl;
            }

            // Precompute the pixel->int8 quantization table (used by
            // make_input on the INT8 NPU path) so per-frame preprocessing does
            // a table lookup instead of a float division per pixel/channel.
            build_quant_lut();

            // Warm up so the NPU compiles/loads the cache before the live loop.
            run_warmup();
            return true;
        } catch (const Ort::Exception& e) {
            std::cerr << "[yolo][error] ORT init failed: " << e.what() << std::endl;
            return false;
        }
    }

    // Precompute the pixel(0..255) -> int8 quantization table:
    //   int8 = clamp(trunc(pixel / quant_scale) + quant_zp, -128, 127)
    void build_quant_lut() {
        const float s  = cfg.quant_scale != 0.0f ? cfg.quant_scale : 1.0f;
        const int   zp = cfg.quant_zp;
        for (int v = 0; v < 256; ++v) {
            int t = static_cast<int>(v / s) + zp;   // truncate toward zero
            quant_lut[v] = static_cast<int8_t>(std::clamp(t, -128, 127));
        }
    }

    // Build the input tensor from an RGB uint8 HWC image, honoring the model's
    // declared input dtype. Backing buffers are members so they stay alive for
    // the following Run() call.
    Ort::Value make_input(const cv::Mat& rgb,
                          const std::array<int64_t, 4>& shape,
                          Ort::MemoryInfo& mem) {
        const int n = imgsz * imgsz;
        const uint8_t* p = rgb.data;

        if (in_elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
            input_i8.resize(static_cast<size_t>(3) * n);
            int8_t* r = input_i8.data();
            int8_t* g = input_i8.data() + n;
            int8_t* b = input_i8.data() + 2 * n;
            const int8_t* lut = quant_lut.data();   // pixel(0..255) -> int8
            for (int i = 0; i < n; ++i) {
                r[i] = lut[p[3 * i + 0]];
                g[i] = lut[p[3 * i + 1]];
                b[i] = lut[p[3 * i + 2]];
            }
            return Ort::Value::CreateTensor<int8_t>(
                mem, input_i8.data(), input_i8.size(),
                shape.data(), shape.size());
        }

        // float32 path
        input_f.resize(static_cast<size_t>(3) * n);
        const float div = cfg.normalize ? 255.0f : 1.0f;
        float* r = input_f.data();
        float* g = input_f.data() + n;
        float* b = input_f.data() + 2 * n;
        for (int i = 0; i < n; ++i) {
            r[i] = p[3 * i + 0] / div;
            g[i] = p[3 * i + 1] / div;
            b[i] = p[3 * i + 2] / div;
        }
        return Ort::Value::CreateTensor<float>(
            mem, input_f.data(), input_f.size(), shape.data(), shape.size());
    }

    void run_warmup() {
        cv::Mat zero(imgsz, imgsz, CV_8UC3, cv::Scalar(0, 0, 0));
        const std::array<int64_t, 4> shape{1, 3, imgsz, imgsz};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = make_input(zero, shape, mem);
        const char* in_names[]  = {input_name};
        const char* out_names[] = {output_name};
        std::cout << "[yolo] warming up (first run compiles/loads the NPU cache)..."
                  << std::endl;
        session.Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
        std::cout << "[yolo] warmup done." << std::endl;
    }

    std::vector<YoloDetection> detect(const cv::Mat& image) {
        if (image.empty()) return {};

        auto t_begin = std::chrono::steady_clock::now();
        auto us_since = [](std::chrono::steady_clock::time_point t) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - t).count();
        };

        // MLPP delivers RGBX (R,G,B,X); OpenCV frames are BGR. Normalize both
        // to an RGB uint8 image, then SIMPLE stretch-resize to the network size
        // (matches the reference prepost, not an aspect-preserving letterbox).
        cv::Mat rgb;
        if (image.channels() == 4) {
            cv::cvtColor(image, rgb, cv::COLOR_RGBA2RGB);   // drop X, keep R,G,B
        } else if (image.channels() == 3) {
            cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
        } else {
            cv::cvtColor(image, rgb, cv::COLOR_GRAY2RGB);
        }
        const long us_cvt = us_since(t_begin);

        auto t_resize = std::chrono::steady_clock::now();
        cv::Mat resized;
        cv::resize(rgb, resized, cv::Size(imgsz, imgsz), 0, 0, cv::INTER_LINEAR);
        const long us_resize = us_since(t_resize);

        auto t_input = std::chrono::steady_clock::now();
        const std::array<int64_t, 4> shape{1, 3, imgsz, imgsz};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = make_input(resized, shape, mem);
        const long us_input = us_since(t_input);

        const char* in_names[]  = {input_name};
        const char* out_names[] = {output_name};

        auto t_run = std::chrono::steady_clock::now();
        auto outs = session.Run(Ort::RunOptions{nullptr},
                                in_names, &in, 1, out_names, 1);
        const long us_run = us_since(t_run);

        auto t_decode = std::chrono::steady_clock::now();
        auto dets = decode(outs.front(), image.cols, image.rows);
        const long us_decode = us_since(t_decode);

#if 0
        std::cout << "[yolo] detect us: cvt=" << us_cvt
                  << " resize=" << us_resize
                  << " make_input=" << us_input
                  << " run=" << us_run
                  << " decode=" << us_decode
                  << " total=" << us_since(t_begin) << std::endl;
#endif

        return dets;
    }

    // Decode a YOLOv8 [1,84,N] or [1,N,84] detection tensor into boxes in the
    // original image's pixel coords (independent x/y stretch back).
    std::vector<YoloDetection> decode(Ort::Value& out, int ow, int oh) {
        const float sx = static_cast<float>(ow) / imgsz;
        const float sy = static_cast<float>(oh) / imgsz;
        auto info = out.GetTensorTypeAndShapeInfo();
        auto shp  = info.GetShape();          // e.g. [1,84,8400]
        const float* data = out.GetTensorData<float>();

        int  feat = 0, num = 0;
        bool feat_major = true;
        if (shp.size() == 3) {
            const int d1 = static_cast<int>(shp[1]);
            const int d2 = static_cast<int>(shp[2]);
            if (d1 == 4 + kNumClasses) { feat = d1; num = d2; feat_major = true; }
            else                       { feat = d2; num = d1; feat_major = false; }
        } else {
            return {};
        }
        if (feat < 4 + 1) return {};
        const int ncls = feat - 4;

        std::vector<cv::Rect2f> boxes;
        std::vector<float>      scores;
        std::vector<int>        classes;
        boxes.reserve(64); scores.reserve(64); classes.reserve(64);

        // Convert a candidate (best class/score + xywh center box in network
        // pixels) into a display-frame box and stash it for NMS.
        auto emit = [&](int best_c, float best_s,
                        float cx, float cy, float bw, float bh) {
            float x1 = (cx - bw * 0.5f) * sx;
            float y1 = (cy - bh * 0.5f) * sy;
            float x2 = (cx + bw * 0.5f) * sx;
            float y2 = (cy + bh * 0.5f) * sy;
            x1 = std::clamp(x1, 0.0f, static_cast<float>(ow - 1));
            y1 = std::clamp(y1, 0.0f, static_cast<float>(oh - 1));
            x2 = std::clamp(x2, 0.0f, static_cast<float>(ow - 1));
            y2 = std::clamp(y2, 0.0f, static_cast<float>(oh - 1));
            if (x2 <= x1 || y2 <= y1) return;
            boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
            scores.push_back(best_s);
            classes.push_back(best_c);
        };

        if (feat_major) {
            // Layout [1, feat, num]: each feature is a contiguous row of `num`
            // floats. Scanning class rows sequentially (instead of gathering
            // all classes for one anchor, which strides by `num` and thrashes
            // the cache) keeps memory access linear.
            dec_best_score.assign(num, -std::numeric_limits<float>::max());
            dec_best_cls.assign(num, 0);
            for (int c = 0; c < ncls; ++c) {
                const float* row = data + static_cast<size_t>(4 + c) * num;
                float* bs = dec_best_score.data();
                int*   bc = dec_best_cls.data();
                for (int a = 0; a < num; ++a) {
                    if (row[a] > bs[a]) { bs[a] = row[a]; bc[a] = c; }
                }
            }
            const float* px = data;
            const float* py = data + static_cast<size_t>(1) * num;
            const float* pw = data + static_cast<size_t>(2) * num;
            const float* ph = data + static_cast<size_t>(3) * num;
            for (int a = 0; a < num; ++a) {
                const float best_s = dec_best_score[a];
                if (best_s < cfg.conf_thr) continue;
                emit(dec_best_cls[a], best_s, px[a], py[a], pw[a], ph[a]);
            }
        } else {
            // Layout [1, num, feat]: each anchor is a contiguous row of `feat`
            // floats, so the per-anchor class scan is already linear.
            for (int a = 0; a < num; ++a) {
                const float* row = data + static_cast<size_t>(a) * feat;
                int   best_c = 0;
                float best_s = row[4];
                for (int c = 1; c < ncls; ++c) {
                    if (row[4 + c] > best_s) { best_s = row[4 + c]; best_c = c; }
                }
                if (best_s < cfg.conf_thr) continue;
                emit(best_c, best_s, row[0], row[1], row[2], row[3]);
            }
        }

        std::vector<YoloDetection> dets;
        std::vector<int> unique_c(classes.begin(), classes.end());
        std::sort(unique_c.begin(), unique_c.end());
        unique_c.erase(std::unique(unique_c.begin(), unique_c.end()),
                       unique_c.end());
        for (int cid : unique_c) {
            std::vector<cv::Rect2f> cb;
            std::vector<float>      cs;
            std::vector<int>        idx;
            for (size_t i = 0; i < classes.size(); ++i) {
                if (classes[i] == cid) {
                    cb.push_back(boxes[i]);
                    cs.push_back(scores[i]);
                    idx.push_back(static_cast<int>(i));
                }
            }
            for (int k : nms(cb, cs, cfg.iou_thr)) {
                YoloDetection d;
                d.box      = boxes[idx[k]];
                d.score    = scores[idx[k]];
                d.class_id = cid;
                dets.push_back(d);
            }
        }
        std::sort(dets.begin(), dets.end(),
                  [](const YoloDetection& a, const YoloDetection& b) {
                      return a.score > b.score;
                  });
        return dets;
    }

    OnnxYolo8nConfig cfg;
    Ort::Env         env{nullptr};
    Ort::Session     session{nullptr};
    std::string      input_name_str;
    std::string      output_name_str;
    const char*      input_name  = nullptr;
    const char*      output_name = nullptr;
    std::vector<int64_t> input_shape;
    ONNXTensorElementDataType in_elem_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    int imgsz = 640;
    std::vector<float>  input_f;
    std::vector<int8_t> input_i8;

    // Pixel(0..255) -> int8 quantization lookup table (built once in init()),
    // replacing a per-pixel float division in make_input().
    std::array<int8_t, 256> quant_lut{};

    // Reused scratch for the cache-friendly decode() best-class scan.
    std::vector<float> dec_best_score;
    std::vector<int>   dec_best_cls;
};

// ---------------------------------------------------------------------------
// Public wrapper
// ---------------------------------------------------------------------------
OnnxYolo8nHelper::OnnxYolo8nHelper(const OnnxYolo8nConfig& cfg)
    : impl_(new Impl(cfg)) {}

OnnxYolo8nHelper::~OnnxYolo8nHelper() { delete impl_; }

bool OnnxYolo8nHelper::init() { return impl_->init(); }

int OnnxYolo8nHelper::input_size() const { return impl_->imgsz; }

std::vector<YoloDetection> OnnxYolo8nHelper::detect(const cv::Mat& image) {
    return impl_->detect(image);
}

const char* OnnxYolo8nHelper::class_name(int class_id) {
    return (class_id >= 0 && class_id < kNumClasses) ? kCocoClasses[class_id]
                                                     : "obj";
}

void OnnxYolo8nHelper::drawDetectionsNV12(cv::Mat& nv12, int nv12_w, int nv12_h,
                                          const std::vector<YoloDetection>& dets,
                                          int src_w, int src_h) {
    if (nv12.empty() || nv12_w <= 0 || nv12_h <= 0 || src_w <= 0 || src_h <= 0)
        return;
    if (nv12.rows < nv12_h + nv12_h / 2 || nv12.cols < nv12_w) return;

    const float sx = static_cast<float>(nv12_w) / src_w;
    const float sy = static_cast<float>(nv12_h) / src_h;

    // Y plane (full res) and interleaved UV plane (half res, 2 channels).
    cv::Mat y_plane = nv12(cv::Rect(0, 0, nv12_w, nv12_h));
    cv::Mat uv_plane(nv12_h / 2, nv12_w / 2, CV_8UC2,
                     nv12.ptr(nv12_h), nv12.step);

    const cv::Rect y_bounds(0, 0, nv12_w, nv12_h);
    const cv::Rect uv_bounds(0, 0, nv12_w / 2, nv12_h / 2);

    // Fill a rectangle on both planes with a solid Y/U/V color.
    auto fill_yuv = [&](const cv::Rect& r, int Y, int U, int V) {
        cv::Rect ry = r & y_bounds;
        if (ry.width > 0 && ry.height > 0)
            y_plane(ry).setTo(cv::Scalar(Y));
        cv::Rect ru(r.x / 2, r.y / 2, r.width / 2, r.height / 2);
        ru &= uv_bounds;
        if (ru.width > 0 && ru.height > 0)
            uv_plane(ru).setTo(cv::Scalar(U, V));
    };

    for (const auto& d : dets) {
        int Y, U, V;
        bgr_to_yuv(color_for(d.class_id), Y, U, V);

        int x1 = static_cast<int>(std::lround(d.box.x * sx));
        int y1 = static_cast<int>(std::lround(d.box.y * sy));
        int x2 = static_cast<int>(std::lround((d.box.x + d.box.width)  * sx));
        int y2 = static_cast<int>(std::lround((d.box.y + d.box.height) * sy));
        x1 = std::clamp(x1, 0, nv12_w - 1);
        y1 = std::clamp(y1, 0, nv12_h - 1);
        x2 = std::clamp(x2, 0, nv12_w - 1);
        y2 = std::clamp(y2, 0, nv12_h - 1);
        if (x2 <= x1 || y2 <= y1) continue;

        // Colored rectangle outline: 4 thin filled bars (keeps Y+UV in sync).
        const int t = 2;  // border thickness (px)
        fill_yuv(cv::Rect(x1, y1, x2 - x1, t), Y, U, V);              // top
        fill_yuv(cv::Rect(x1, y2 - t, x2 - x1, t), Y, U, V);         // bottom
        fill_yuv(cv::Rect(x1, y1, t, y2 - y1), Y, U, V);            // left
        fill_yuv(cv::Rect(x2 - t, y1, t, y2 - y1), Y, U, V);       // right

        // Label background bar + text (text drawn on the Y plane only).
        char label[64];
        std::snprintf(label, sizeof(label), "%s %.2f",
                      class_name(d.class_id), d.score);
        int base = 0;
        const cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                            0.5, 1, &base);
        const int bar_h = ts.height + base + 4;
        int by = std::max(0, y1 - bar_h);
        fill_yuv(cv::Rect(x1, by, ts.width + 6, bar_h), Y, U, V);
        cv::putText(y_plane, label, cv::Point(x1 + 3, by + ts.height + 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(16), 1,
                    cv::LINE_AA);
    }
}
