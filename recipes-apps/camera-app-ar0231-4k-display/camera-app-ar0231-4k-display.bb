# camera_8_example_app.bb
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

SUMMARY = "8 Camera AR0231 Example"
DESCRIPTION = "Uses OpenCL to scale the cameras to be displayed on a 4K display"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Source file
SRC_URI = "file://camera_8_example_app.cpp \
        file://camera_4_example_app.cpp \
        file://drm_helper.cpp \
        file://drm_helper.h \
        file://v4l2_helper.cpp \
        file://v4l2_helper.h \
        file://CMakeLists.txt \
        "

S = "${WORKDIR}"

# Dependencies
DEPENDS = "opencv libdrm gstreamer1.0 gstreamer1.0-plugins-base"

inherit cmake pkgconfig
