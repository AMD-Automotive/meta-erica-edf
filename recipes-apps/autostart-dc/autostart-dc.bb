# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, TO
# Maintainer: KC

#
# This file is the autostart-dc recipe.
#

SUMMARY = "autostart script to set up environment at boot"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://./"

inherit systemd

S = "${WORKDIR}"
FILES:${PN} += "${bindir}/* \
	       ${sysconfdir}/autostart-dc.conf \
	       ${systemd_unitdir}/system/autostart.service \
	       "

CONFFILES:${PN} += "${sysconfdir}/autostart-dc.conf"

SYSTEMD_SERVICE:${PN} = "autostart.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
	install -d ${D}${systemd_unitdir}/system
	install -m 0644 ${S}/autostart.service ${D}/${systemd_unitdir}/system

	install -d ${D}${bindir}
	install -m 0755 ${S}/autostart-dc.sh ${D}${bindir}/autostart-dc.sh
	install -m 0755 ${S}/config_pf_rerouting.sh ${D}${bindir}/config_pf_rerouting.sh
	install -m 0755 ${S}/config-camera-tpg.sh ${D}${bindir}/config-camera-tpg.sh
	install -m 0755 ${S}/config-camera-quanta_imx623.sh ${D}${bindir}/config-camera-quanta_imx623.sh
	install -m 0755 ${S}/config-camera-leopard_imx623.sh ${D}${bindir}/config-camera-leopard_imx623.sh
	install -m 0755 ${S}/init_cameras_minimal.sh ${D}${bindir}/init_cameras_minimal.sh

	install -d ${D}${sysconfdir}
	install -m 0644 ${S}/autostart-dc.conf ${D}${sysconfdir}/autostart-dc.conf

}
RDEPENDS:${PN} += "bash"
