# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

SUMMARY = "Resize SD Startup Script"
SECTION = "PETALINUX/setting "

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./"

inherit systemd

S = "${WORKDIR}"

FILES:${PN} += "${bindir}/* \
	       ${systemd_unitdir}/system/resize-sd.service \
	       "

RDEPENDS:${PN} += " bash parted e2fsprogs-resize2fs"

SYSTEMD_SERVICE:${PN} = "resize-sd.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"


do_install() {
	install -d ${D}${systemd_unitdir}/system
	install -m 0644 ${S}/resize-sd.service ${D}/${systemd_unitdir}/system

	install -d ${D}${bindir}
	install -m 0755 ${S}/resize-sd.sh ${D}${bindir}/resize-sd.sh
}