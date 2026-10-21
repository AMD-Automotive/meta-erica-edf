# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, TO
# Maintainer: KC

#
# This file is the autostart recipe.
#

SUMMARY = "autostart script to set up environment at boot"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./"

inherit systemd

S = "${WORKDIR}"
FILES:${PN} += "${bindir}/* \
	       ${systemd_unitdir}/system/autostart.service \
	       "

SYSTEMD_SERVICE:${PN} = "autostart.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
	install -d ${D}${systemd_unitdir}/system
	install -m 0644 ${S}/autostart.service ${D}/${systemd_unitdir}/system

	install -d ${D}${bindir}
	install -m 0755 ${S}/autostart.sh ${D}${bindir}/autostart.sh
	install -m 0755 ${S}/autostart-vek385-base-revb.sh ${D}${bindir}/autostart-vek385-base-revb.sh
	install -m 0755 ${S}/vek385_read_mode_gpio.sh ${D}${bindir}/vek385_read_mode_gpio.sh

}
RDEPENDS:${PN} += "bash"

