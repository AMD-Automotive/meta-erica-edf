# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

FILESEXTRAPATHS:prepend:vek385-base-revb := "${THISDIR}/u-boot-xlnx/vek385-base-revb:"

SRC_URI:append:vek385-base-revb = " \
    file://0001-Restrict-dtb-to-a-third-vs-a-quarter.patch \
"