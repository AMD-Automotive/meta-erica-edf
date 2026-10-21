# CMakeLists.txt
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# Build configuration for Camera example application
# Author: LT Thomas

cmake_minimum_required(VERSION 3.10)
project(CameraExamples)

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


add_executable(camera-app-ar0231-4k-display camera_8_example_app.cpp drm_helper.cpp v4l2_helper.cpp)
add_executable(ptest-camera-quad-ar0231-4k-display camera_4_example_app.cpp drm_helper.cpp v4l2_helper.cpp)

# Create a list of all targets for easier management
set(ALL_TARGETS 
    camera-app-ar0231-4k-display
    ptest-camera-quad-ar0231-4k-display
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
