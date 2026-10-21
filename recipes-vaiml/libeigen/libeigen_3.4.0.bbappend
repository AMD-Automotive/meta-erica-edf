# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

# This .bbappend updates the SRCREV for Eigen
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

LIC_FILES_CHKSUM = "file://COPYING.BSD;md5=2dd0510ee95e59ca28834b875bc96596 \
                    file://COPYING.APACHE;md5=8de23b8e93c63005353056b2475e9aa5 \
                    file://COPYING.MINPACK;md5=71d91b0f75ce79a75d3108a72bef8116 \
		    file://COPYING.MPL2;md5=48a3fe23ed1353e0995dadfda05ffdb6 \
		   "

SRC_URI = " \
   git://gitlab.com/libeigen/eigen.git;protocol=http;branch=master \ 
   file://0001-Fix-eigen_hardware_destructive_interference_size-iss.patch \
"

SRCREV = "84282c42fc05d08216331203206de7ceb024a9c6"

INSANE_SKIP:${PN}-dev += "dev-elf"
