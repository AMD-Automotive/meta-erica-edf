# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

cmake_minimum_required(VERSION 3.10)
project(opencl_image_resize CXX)

# Find OpenCV package
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs videoio)

# Add executable
add_executable(opencl_image_resize opencl-image-resize.cpp)

# Link to OpenCV libraries
target_link_libraries(opencl_image_resize PRIVATE ${OpenCV_LIBS})

# Set the C++ standard to C++11 (or a higher version if needed)
set_property(TARGET opencl_image_resize PROPERTY CXX_STANDARD 11)
set_property(TARGET opencl_image_resize PROPERTY CXX_STANDARD_REQUIRED ON)

install(TARGETS opencl_image_resize RUNTIME DESTINATION bin)