# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

DESCRIPTION = "ERICA-H Xen Minimal Image"

require edf-image-common.inc
require rootfs_id.inc

IMAGE_FEATURES += "ssh-server-openssh package-management"
IMAGE_FSTYPES = "wic"

IMAGE_INSTALL = "\
    packagegroup-core-boot \
    packagegroup-core-full-cmdline \
    ${AMD-EDF_IMAGE_COMMON_INSTALL} \
    packagegroup-xen \    
    devmem2 \
    ssh-rootlogin \
    esync-agent \
    "
WKS_FILES = "erica-default-sd.wks"

TOOLCHAIN_HOST_TASK:append = "\
    "

EXTRA_IMAGE_FEATURES:append = " \
    empty-root-password \
    serial-autologin-root \
    "

inherit core-image

do_rootfs[prefuncs] += "edf_check_rootfs"
