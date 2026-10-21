# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

DESCRIPTION = "systemd-networkd configuration for RAVE2 board networking"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://10-eth1-preferred.network"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/10-eth1-preferred.network ${D}${sysconfdir}/systemd/network/
}

FILES:${PN} = "${sysconfdir}/systemd/network/"

RDEPENDS:${PN} = "systemd"
