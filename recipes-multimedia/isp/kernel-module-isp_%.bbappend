# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT, JC
# Maintainer: LT

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:amdtool-2025_2 = " \
            file://0001-LT-Updating-driver-to-ignore-stream-off-and-keep-ISP.patch \
            file://0001-RPU-Timestamping-and-Jitter-testing.patch \
            file://0001-ISP-Driver-Timestamping-without-drift-improvement.patch \
            file://0001-Payload-Improvements-Kernel-Crash-Fixes-Debug-Kernel.patch \
"

# 2026.1 (kernel 6.18) ISP driver patches: async-notifier DT-disabled-node fix
# and single-buffer min_queued_buffers module param.
SRC_URI:append:amdtool-2026_1 = " \
            file://0011-visp-skip-DT-disabled-upstream-nodes-in-ISP-async-no.patch \
            file://0012-visp_video-single-buffer-min_queued_buffers-param.patch \
            file://0013-Start-of-Frame-timestamp-and-sensor-stop-start-chang.patch \
"
