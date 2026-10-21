# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, TO
# Maintainer: KC

SUMMARY = "VEK385 PLF files"
LICENSE = "BSD-3-Clause & MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9 \
                    file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./"

S = "${WORKDIR}"

do_install () {
        install -d ${D}${datadir}
        install -d ${D}${datadir}/${PN}
        install -m 0644 ${S}/*.ini ${D}${datadir}/${PN}
        install -m 0644 ${S}/*.xml ${D}${datadir}/${PN}

        install -d ${D}${bindir}
        install -m 0755 ${S}/vek385_configure_cameras ${D}${bindir}
}

FILES:${PN} = "/"
