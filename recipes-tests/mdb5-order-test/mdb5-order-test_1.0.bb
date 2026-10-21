# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe video bridge frame-order soak / isolation test"
DESCRIPTION = "Detects intermittent frame reordering on the video bridge using \
the endpoint in-band watermark, and sweeps endpoint/host knobs to isolate the \
frame-buffer-reuse race from the camera"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://mdb5_order_soak.py \
    file://mdb5_order_check.c \
    file://mdb5_frame_wm.h \
"

S = "${WORKDIR}"

RDEPENDS:${PN} += "\
    python3-core \
    python3-json \
    ep-proxy \
    kernel-module-vivid \
    v4l-utils \
"

INSTALL_DIR = "${datadir}/mdb5-order-test"

do_install() {
    install -d ${D}${INSTALL_DIR}
    install -m 0755 ${WORKDIR}/mdb5_order_soak.py ${D}${INSTALL_DIR}/
    install -m 0644 ${WORKDIR}/mdb5_order_check.c ${D}${INSTALL_DIR}/
    install -m 0644 ${WORKDIR}/mdb5_frame_wm.h ${D}${INSTALL_DIR}/
}

FILES:${PN} += "${INSTALL_DIR}"
