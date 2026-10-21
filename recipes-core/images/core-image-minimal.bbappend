# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

# core-image-minimal is built for several targets, some of which need a plain
# SD-card .wic image. The AMD BSP machine default IMAGE_FSTYPES only emits
# wic.qemu-sd and wic.ufs (no bare wic), so add a plain wic using the standard
# EDF SD partition layout.
IMAGE_FSTYPES += "wic wic.xz wic.bmap"
WKS_FILES = "erica-default-sd.wks"
