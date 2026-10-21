# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe video bridge endpoint proxy"
DESCRIPTION = "Multi-channel V4L2 capture proxy for Versal-to-host \
video streaming over PCIe BRAM mailbox and MSI-X"
LICENSE = "MIT & GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
                    file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = " \
    file://ep_proxy.c \
    file://ep_proxy.h \
    file://mdb5_bridge.h \
    file://Makefile \
    file://mdb5_4imx623_1792.json \
    file://mdb5_4imx623_1920.json \
    file://mdb5_1imx728_4imx623_1920.json \
    file://mdb5_1imx728_4imx623_1792.json \
"

S = "${WORKDIR}"

# ep_proxy.h includes <linux/videodev2.h>
DEPENDS = "virtual/kernel"

do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ep_proxy ${D}${bindir}/ep_proxy

    install -d ${D}${sysconfdir}
    install -m 0644 ${S}/mdb5_4imx623_1792.json ${D}${sysconfdir}/mdb5_4imx623_1792.json
    install -m 0644 ${S}/mdb5_4imx623_1920.json ${D}${sysconfdir}/mdb5_4imx623_1920.json
    install -m 0644 ${S}/mdb5_1imx728_4imx623_1920.json ${D}${sysconfdir}/mdb5_1imx728_4imx623_1920.json
    install -m 0644 ${S}/mdb5_1imx728_4imx623_1792.json ${D}${sysconfdir}/mdb5_1imx728_4imx623_1792.json
}
