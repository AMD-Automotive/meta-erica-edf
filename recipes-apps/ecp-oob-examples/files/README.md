<!--
Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
SPDX-License-Identifier: MIT
Contributors: LT
Maintainer: LT
-->

# ECP Out-of-Box Examples

A collection of C++ example applications for the Erica Common Platform (ECP)
demonstrating camera capture (ISP/V4L2), hardware video decode (VCU),
Machine Learning Pre-Processing (MLPP), and DRM display output on AMD/Xilinx
platforms (e.g. VEK385).

The examples capture and/or process NV12 video and render it to a DRM display,
in either RGB888 or NV12 output formats at 1080p or 4K.

## Shared Helpers

Most examples are built on a small set of reusable helper modules:

| Helper | Purpose |
| --- | --- |
| [drm_helper.cpp](drm_helper.cpp) | DRM display init and output (RGB888/NV12, 1080p/4K, 30/60 FPS), plus NV12 overlay/blit utilities. |
| [isp_helper.cpp](isp_helper.cpp) | V4L2 camera capture and video-device management. |
| [mlpp_helper.cpp](mlpp_helper.cpp) | Machine Learning Pre-Processing (MLPP) hardware interface (scale, crop, format conversion). |
| [vcu_helper.cpp](vcu_helper.cpp) | Video Codec Unit (VCU) hardware decode/encode. |
| [colorbar_helper.cpp](colorbar_helper.cpp) | Color-bar test-pattern generation. |
| [onnx_yolo8n_helper.cpp](onnx_yolo8n_helper.cpp) | YOLOv8-nano inference via ONNX Runtime with the Vitis AI (VAIML) NPU execution provider, plus NV12 detection overlay. |
| [ep_proxy_bridge.cpp](ep_proxy_bridge.cpp) | PCIe frame-sharing bridge (MDB5 endpoint): publishes captured frames to a host over PCIe. |

## Examples

### ISP / Camera capture
| Target | Description |
| --- | --- |
| `imx623_opencv_1080p_drm` | IMX623 capture via OpenCV, NV12 from ISP, 1080p DRM. |
| `imx623_v4l2_1080p_drm` | IMX623 capture via V4L2, NV12 from ISP, 1080p DRM. |
| `imx728_v4l2_1080p_drm` | IMX728 capture via V4L2, NV12 from ISP, 1080p RGB DRM. |
| `imx728_v4l2_4k_drm` | IMX728 capture via V4L2, NV12 from ISP, 4K NV12 DRM. |
| `imx623_quad_v4l2_4k_drm` | 4x IMX623 via DMABUF/V4L2, tiled 2x2 on a 1080p display. |

### VCU (hardware video decode)
| Target | Description |
| --- | --- |
| `vcu_demo_opencv_4k_nv12in_4k_nv12_drm` | 4K NV12 processing with DRM display output. |

### MLPP (pre-processing)
| Target | Description |
| --- | --- |
| `mlpp_display_output` | MLPP preprocessing with DRM display output. |
| `mlpp_5display_output` | MLPP with all inputs / surround-view inputs varied. |
| `mlpp_nhwc4_npu_output` | MLPP with NHWC4 output format. |
| `mlpp_dynamic_scale` | Dynamic scale-parameter changes during MLPP processing. |
| `mlpp_dynamic_crop` | Dynamic crop-parameter changes during MLPP processing. |
| `mlpp_color_scale_test` | MLPP with scaled color outputs and DRM display. |

### ISP + MLPP
| Target | Description |
| --- | --- |
| `imx728_v4l2_mlpp_1080p_drm` | IMX728 V4L2 capture with MLPP, 1080p DRM. |
| `imx623_v4l2_mlpp_1080p_drm` | IMX623 V4L2 capture with MLPP, 1080p DRM. |
| `5cam_imx_v4l2_mlpp_1080p_drm` | 5 cameras (4x IMX623 + 1x IMX728 4K) with MLPP, composited on a 1080p display. |
| `5cam_imx_v4l2_mlpp_4k_drm` | 5 cameras (4x IMX623 + 1x IMX728 4K) with MLPP, composited on a 4K display. |

### AI / NPU (ONNX Runtime)
Built only when ONNX Runtime is found (see [Building](#building)). These add
per-frame YOLOv8-nano object detection on the AMD NPU via the Vitis AI (VAIML)
execution provider.

| Target | Description |
| --- | --- |
| `imx623_v4l2_mlpp_onnx_1080p_drm` | IMX623 V4L2 + MLPP with YOLOv8-nano NPU detection, 1080p DRM. |
| `5cam_imx_v4l2_mlpp_onnx_1080p_drm` | 5 cameras + MLPP with YOLOv8-nano NPU detection, composited on a 1080p display. |

### VCU + MLPP
| Target | Description |
| --- | --- |
| `vcu_demo_opencv_mlpp_nv12_display` | NV12 video processing with MLPP display scaling. |
| `vcu_demo_opencv_mlpp_nv12_ffcoutput` | NV12 with fast frame copy, crop scaling, and multiple outputs. |

> Additional experimental targets (dual IMX728, twelve IMX623, 5-camera DMABUF,
> GStreamer pipelines) are present in the sources but commented out in
> [CMakeLists.txt](CMakeLists.txt). Uncomment their `add_executable` and
> `ALL_TARGETS` entries to build them.

## Command-line options

Many of the V4L2 capture examples accept common flags:

- `--ep_proxy` — enable the PCIe frame-sharing bridge (`EpProxyBridge`), which
  publishes each captured frame to a host over PCIe (BRAM mailbox + MSI-X). It
  is **off by default**; without the flag the examples run local-only.
- `--no-display` — skip DRM display output (capture/log only), on the
  single-camera examples that support it.
- `-d /dev/videoX` — select the capture device (on the examples that take it).
- `<camera_id>` — on `imx623_v4l2_1080p_drm` and the ONNX single-camera
  example, a positional `0..11` selects which `/dev/video*` to open.

Run any example with `-h` / `--help` for its exact usage.

## Dependencies

- CMake >= 3.10, C++11 toolchain
- OpenCV (`core`, `imgproc`, `highgui`, `imgcodecs`, `videoio`)
- libdrm
- GStreamer 1.0 (`gstreamer-1.0`, `-app`, `-video`, `-allocators`)
- Linux V4L2 headers (`linux/videodev2.h`)
- ONNX Runtime (optional) — required only for the AI/NPU examples. The C++ API
  needs C++17. On the Vitis rootfs `libonnxruntime.so` lives in `/usr/lib` with
  headers under `/usr/include/onnxruntime/core/session/`.

## Building

### On the target (VEK385)

```sh
mkdir build
cd build
cmake ..
make -j6
```

### Cross-compiling with the Yocto SDK

Source the SDK environment first, then configure and build as above:

```sh
source /home/amd/projects/Yocto_SDK/environment-setup-cortexa72-cortexa53-xilinx-linux
mkdir build
cd build
cmake ..
make -j6
```

Built binaries are placed in the `build` directory. `make install` installs all
targets to `bin`.

### AI / NPU (ONNX Runtime) examples

The ONNX examples build automatically when ONNX Runtime is detected. On the
Vitis target it is found in the default paths. Elsewhere, point CMake at an
ONNX Runtime install (a directory containing `include/` and `lib/`):

```sh
cmake -DONNXRUNTIME_ROOT=<path> ..
```

If ONNX Runtime is not found, those targets are skipped with a warning and the
rest of the build proceeds normally.

#### Runtime environment

The ONNX examples set the required runtime environment (`XRT_AIARM`,
`LD_LIBRARY_PATH`, `XLNX_ENABLE_CACHE`, `XRT_ELF_FLOW`) themselves and re-exec
once on startup so the dynamic loader picks it up — no manual `export` needed.

## Running

Run any built example directly on the target, which requires a connected DRM
display and the relevant camera/VCU hardware:

```sh
./imx623_v4l2_1080p_drm
```
