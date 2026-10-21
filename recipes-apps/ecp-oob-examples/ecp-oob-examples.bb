# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

SUMMARY = "Erica Common Platform Examples"
LICENSE = "MIT & GPL-2.0-only & Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
                    file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6 \
                    file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "file://./"

S = "${WORKDIR}"

do_install () {
        install -d ${D}${datadir}
        install -d ${D}${datadir}/${PN}
        install -m 0644 ${S}/*.txt ${D}${datadir}/${PN}
        install -m 0644 ${S}/*.h ${D}${datadir}/${PN}
        install -m 0644 ${S}/*.cpp ${D}${datadir}/${PN}
}

FILES:${PN} = "/"