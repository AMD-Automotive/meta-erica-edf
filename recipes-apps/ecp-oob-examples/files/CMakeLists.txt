# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: KC

# CMakeLists.txt
# Build configuration for Erica Common Platform example applications and video processing utilities.
# Author: LT Thomas

cmake_minimum_required(VERSION 3.10)
project(ECPExamples)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV installation and include both imgproc and core modules
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs videoio)
find_package(PkgConfig REQUIRED) 
pkg_check_modules( LIBDRM REQUIRED libdrm )
pkg_check_modules( GSTREAMER REQUIRED gstreamer-1.0 )
pkg_check_modules( GSTREAMER_APP REQUIRED gstreamer-app-1.0 )
pkg_check_modules( GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0 )
pkg_check_modules( GSTREAMER_ALLOCATORS REQUIRED gstreamer-allocators-1.0 )

# Check if V4L2 headers are available
include(CheckIncludeFile)
check_include_file("linux/videodev2.h" HAVE_V4L2)
if(NOT HAVE_V4L2)
    message(FATAL_ERROR "V4L2 headers not found. Please install linux-headers or kernel-headers package.")
endif()


# ISP Examples
add_executable(imx623_opencv_1080p_drm single_imx623_opencv_1080p_drm.cpp drm_helper.cpp)
add_executable(imx728_v4l2_1080p_drm single_imx728_v4l2_1080p_drm.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)
add_executable(imx728_v4l2_4k_drm single_imx728_v4l2_4k_drm.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)
add_executable(imx623_v4l2_1080p_drm single_imx623_v4l2_1080p_drm.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)
add_executable(imx623_quad_v4l2_4k_drm quad_imx623_v4l2_4k_drm.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)
#add_executable(imx728_dual_v4l2_4k_drm dual_imx728_v4l2_4k_drm.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)
#add_executable(imx623_twelve_v4l2 twelve_imx623_v4l2.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp mlpp_helper.cpp)


# VCU Examples
add_executable(vcu_demo_opencv_4k_nv12in_4k_nv12_drm vcu_opencv_4k_nv12in_4k_nv12_drm.cpp drm_helper.cpp mlpp_helper.cpp)

# MLPP Examples
add_executable(mlpp_display_output mlpp_display_output.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)
add_executable(mlpp_5display_output mlpp_5display_output.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)
add_executable(mlpp_nhwc4_npu_output mlpp_nhwc4_output.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)
add_executable(mlpp_dynamic_scale mlpp_dynamic_scale_example.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)
add_executable(mlpp_dynamic_crop mlpp_dynamic_crop_example.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)
add_executable(mlpp_color_scale_test mlpp_color_scale.cpp mlpp_helper.cpp drm_helper.cpp colorbar_helper.cpp)


# ISP + MLPP Examples
add_executable(imx728_v4l2_mlpp_1080p_drm single_imx728_v4l2_mlpp_1080p_drm.cpp mlpp_helper.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp)
add_executable(imx623_v4l2_mlpp_1080p_drm single_imx623_v4l2_mlpp_1080p_drm.cpp mlpp_helper.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp)
add_executable(5cam_imx_v4l2_mlpp_4k_drm 5cam_imx_v4l2_mlpp_4k_drm.cpp mlpp_helper.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp)
add_executable(5cam_imx_v4l2_mlpp_1080p_drm 5cam_imx_v4l2_mlpp_1080p_drm.cpp mlpp_helper.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp)

# VCU + MLPP Examples
add_executable(vcu_demo_opencv_mlpp_nv12_display vcu_nv12_display_scale.cpp mlpp_helper.cpp drm_helper.cpp)
add_executable(vcu_demo_opencv_mlpp_nv12_ffcoutput vcu_nv12_ffc_cropscale_outputs.cpp mlpp_helper.cpp drm_helper.cpp)
#add_executable(vcu_demo_opencv_mlpp_nv12_1ffc_4svc_display vcu_nv12_1ffc_4svc_display_scale.cpp mlpp_helper.cpp drm_helper.cpp)
#add_executable(vcu_demo_gst_mlpp__nv12_1ffc_4svc_display vcu_gst_nv12_5vid_display.cpp mlpp_helper.cpp drm_helper.cpp vcu_helper.cpp)


# Create a list of all targets for easier management
set(ALL_TARGETS 
    mlpp_nhwc4_npu_output
    mlpp_display_output
    mlpp_5display_output
    mlpp_dynamic_scale
    mlpp_dynamic_crop
    mlpp_color_scale_test

    imx623_v4l2_1080p_drm
    imx728_v4l2_4k_drm
    imx728_v4l2_1080p_drm
    imx623_opencv_1080p_drm
    imx623_quad_v4l2_4k_drm
    #imx728_dual_v4l2_4k_drm

    vcu_demo_opencv_4k_nv12in_4k_nv12_drm
    vcu_demo_opencv_mlpp_nv12_display
    vcu_demo_opencv_mlpp_nv12_ffcoutput
    #vcu_demo_opencv_mlpp_nv12_1ffc_4svc_display
    #vcu_demo_gst_mlpp_nv12_1ffc_4svc_display
    
    imx728_v4l2_mlpp_1080p_drm
    imx623_v4l2_mlpp_1080p_drm
    5cam_imx_v4l2_mlpp_4k_drm
    5cam_imx_v4l2_mlpp_1080p_drm
)

# Include DRM headers for all targets
foreach(target ${ALL_TARGETS})
    target_include_directories(${target} PRIVATE ${LIBDRM_INCLUDE_DIRS} ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_APP_INCLUDE_DIRS} ${GSTREAMER_VIDEO_INCLUDE_DIRS} ${GSTREAMER_ALLOCATORS_INCLUDE_DIRS})
endforeach()

# Link against the OpenCV core and imgproc modules, DRM libraries, and system libraries for V4L2
foreach(target ${ALL_TARGETS})
    target_link_libraries(${target} 
        ${OpenCV_LIBS} 
        ${LIBDRM_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_APP_LIBRARIES}
        ${GSTREAMER_VIDEO_LIBRARIES}
        ${GSTREAMER_ALLOCATORS_LIBRARIES}
        pthread  # Required for std::thread
    )
endforeach()

# Install all targets
install(TARGETS ${ALL_TARGETS} DESTINATION bin)


# ---------------------------------------------------------------------------
# YOLOv8-nano ONNX/NPU demo (imx728_v4l2_mlpp_onnx_4k_drm)
#
# Same MLPP/DRM/PCIe pipeline as imx728_v4l2_mlpp_4k_drm, plus per-frame
# YOLOv8-nano inference via ONNX Runtime with the Vitis AI (VAIML) Execution
# Provider. Only built when ONNX Runtime is found.
#
#   * Target Vitis rootfs: libonnxruntime.so in /usr/lib, headers under
#     /usr/include/onnxruntime/core/session/.
#   * Otherwise point at an ONNX Runtime install with -DONNXRUNTIME_ROOT=<path>
#     (a directory containing include/ and lib/).
# ---------------------------------------------------------------------------
set(ONNXRUNTIME_ROOT "" CACHE PATH
    "ONNX Runtime install root (contains include/ and lib/)")
find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    HINTS ${ONNXRUNTIME_ROOT}/lib ${ONNXRUNTIME_ROOT}/lib64
    PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 /opt/onnxruntime/lib)
find_path(ONNXRUNTIME_INCLUDE
    NAMES onnxruntime/core/session/onnxruntime_cxx_api.h
          core/session/onnxruntime_cxx_api.h
          onnxruntime_cxx_api.h
    HINTS ${ONNXRUNTIME_ROOT}/include
    PATHS /usr/include /usr/include/onnxruntime /usr/local/include
          /usr/local/include/onnxruntime /opt/onnxruntime/include)

if(ONNXRUNTIME_LIB AND ONNXRUNTIME_INCLUDE)
    message(STATUS "ONNX Runtime lib: ${ONNXRUNTIME_LIB}")
    message(STATUS "ONNX Runtime include: ${ONNXRUNTIME_INCLUDE}")

    # Directory holding libonnxruntime.so, baked into the RPATH below so the
    # binaries find it at runtime even outside the default loader path.
    get_filename_component(ONNXRUNTIME_LIBDIR "${ONNXRUNTIME_LIB}" DIRECTORY)

    # Build one ONNX/NPU example: add_onnx_example(<target> <main_source>).
    # Each shares the same helper sources, C++17 requirement, include dirs,
    # link libraries and RPATH.
    function(add_onnx_example target main_source)
        add_executable(${target}
            ${main_source}
            mlpp_helper.cpp drm_helper.cpp isp_helper.cpp ep_proxy_bridge.cpp
            onnx_yolo8n_helper.cpp)

        # ONNX Runtime C++ API needs C++17 (std::clamp, etc.).
        set_target_properties(${target} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            BUILD_RPATH "${ONNXRUNTIME_LIBDIR}"
            INSTALL_RPATH "${ONNXRUNTIME_LIBDIR}")

        target_include_directories(${target} PRIVATE
            ${LIBDRM_INCLUDE_DIRS}
            ${GSTREAMER_INCLUDE_DIRS}
            ${GSTREAMER_APP_INCLUDE_DIRS}
            ${GSTREAMER_VIDEO_INCLUDE_DIRS}
            ${GSTREAMER_ALLOCATORS_INCLUDE_DIRS}
            ${ONNXRUNTIME_INCLUDE})

        target_link_libraries(${target}
            ${OpenCV_LIBS}
            ${LIBDRM_LIBRARIES}
            ${GSTREAMER_LIBRARIES}
            ${GSTREAMER_APP_LIBRARIES}
            ${GSTREAMER_VIDEO_LIBRARIES}
            ${GSTREAMER_ALLOCATORS_LIBRARIES}
            ${ONNXRUNTIME_LIB}
            pthread)

        install(TARGETS ${target} DESTINATION bin)
    endfunction()

    add_onnx_example(imx623_v4l2_mlpp_onnx_1080p_drm
        single_imx623_v4l2_mlpp_onnx_1080p_drm.cpp)
    add_onnx_example(5cam_imx_v4l2_mlpp_onnx_1080p_drm
        5cam_imx_v4l2_mlpp_onnx_1080p_drm.cpp)
else()
    message(WARNING "ONNX Runtime not found; skipping imx623_v4l2_mlpp_onnx_1080p_drm. "
                    "Set -DONNXRUNTIME_ROOT=<path> to enable it.")
endif()
