# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

FILESEXTRAPATHS:prepend  := "${THISDIR}/files:"

SRC_URI:append = " file://visp-blacklist.conf"

do_install:append() {
    install -d ${D}${sysconfdir}/modprobe.d
    install -m 0644 ${WORKDIR}/visp-blacklist.conf ${D}${sysconfdir}/modprobe.d/
}

FILES:${PN} += "${sysconfdir}/modprobe.d/visp-blacklist.conf"