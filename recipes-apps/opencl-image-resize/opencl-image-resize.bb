# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "OpenCL Image Resize"
DESCRIPTION = "Resizes an image using OpenCL"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Source file
SRC_URI = "file://opencl-image-resize.cpp \
        file://CMakeLists.txt \
        "

S = "${WORKDIR}"

# Dependencies
DEPENDS = "opencv"

inherit cmake