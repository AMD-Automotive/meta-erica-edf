# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Video Config Script"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./ \
"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${bindir}
	install -m 0755 ${S}/video_config.py ${D}${bindir}/video_config
}

RDEPENDS:${PN} += " python3-smbus2"

FILES:${PN} = "${datadir}/* ${bindir}/*"