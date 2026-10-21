# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe video bridge end-to-end test"
DESCRIPTION = "Validates ep_proxy with vivid virtual V4L2 sources across \
multiple format, resolution, channel-count, and API-mode combinations"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://mdb5_e2e_test.py \
    file://mdb5_test_matrix.json \
"

S = "${WORKDIR}"

RDEPENDS:${PN} += "\
    python3-core \
    python3-json \
    ep-proxy \
    kernel-module-vivid \
    v4l-utils \
"

INSTALL_DIR = "${datadir}/mdb5-video-test"

do_install() {
    install -d ${D}${INSTALL_DIR}
    install -m 0755 ${WORKDIR}/mdb5_e2e_test.py ${D}${INSTALL_DIR}/
    install -m 0644 ${WORKDIR}/mdb5_test_matrix.json ${D}${INSTALL_DIR}/
}

FILES:${PN} += "${INSTALL_DIR}"
