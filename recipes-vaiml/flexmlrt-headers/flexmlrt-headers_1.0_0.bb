# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Add FlexmlRT headers to sysrrot"
DESCRIPTION = "FlexmlRT headers added in sysroot to build vart-x"
LICENSE = "MIT"

SRC_URI = " \
            file://FlexMLClient.h \
            file://LICENSE \
            "
S = "${WORKDIR}"

LIC_FILES_CHKSUM = "file://LICENSE;md5=2d15a13a2ce63308603da20153ec9907"

do_install() {
    install -d ${D}${includedir}
    install -m 0644 ${S}/FlexMLClient.h ${D}${includedir}/
}

FILES:${PN}-dev += "${includedir}/*"
ALLOW_EMPTY:${PN} = "1"
