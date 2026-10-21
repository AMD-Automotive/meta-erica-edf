# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Append AMD heap name to /etc/environment"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

ALLOW_EMPTY:${PN} = "1"

pkg_postinst:${PN}() {
    if [ -n "$D" ]; then
        install -d $D${sysconfdir}
        echo 'AMD_HEAPS_MEMORY_HEAP_NAME="shared_dma_mem_region"' >> $D${sysconfdir}/environment
    fi
}