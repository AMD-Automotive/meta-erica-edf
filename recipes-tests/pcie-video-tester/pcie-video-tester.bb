# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe video tester application"
DESCRIPTION = "Multi-channel V4L2 capture application for Versal-to-host \
Vivid virtual device video streaming over PCIe BRAM mailbox and MSI-X"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ep_vivid_setup.sh \
    file://mdb5_vivid_channels.json \
"

RDEPENDS:${PN} += "bash"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ep_vivid_setup.sh ${D}${bindir}/ep_vivid_setup.sh

    install -d ${D}${sysconfdir}
    install -m 0644 ${S}/mdb5_vivid_channels.json ${D}${sysconfdir}/mdb5_vivid_channels.json
}
