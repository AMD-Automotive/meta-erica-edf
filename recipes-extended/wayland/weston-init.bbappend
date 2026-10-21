# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"

SRC_URI += "file://weston-drm-launch"

do_install:append() {
    install -Dm 0755 ${WORKDIR}/weston-drm-launch ${D}${bindir}/weston-drm-launch
}

FILES:${PN} += "${bindir}/weston-drm-launch"