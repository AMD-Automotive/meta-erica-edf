# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Install PCIe programmer utility"
DESCRIPTION = "Installs programmer.py as /usr/sbin/programmer on the target"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://programmer.py \
           file://rave2-pcie-programmer.service \
           file://recovery-init \
           "

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "rave2-pcie-programmer.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/programmer.py ${D}${sbindir}/programmer

    # PID-1 init wrapper used by the rave2-recovery image (kernel cmdline
    # init=${base_sbindir}/recovery-init bypasses systemd). Harmless on other
    # images. Use ${base_sbindir} so the path obeys the usrmerge distro
    # feature (resolves to /usr/sbin under usrmerge, /sbin otherwise).
    install -d ${D}${base_sbindir}
    install -m 0755 ${WORKDIR}/recovery-init ${D}${base_sbindir}/recovery-init

    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/rave2-pcie-programmer.service ${D}${systemd_unitdir}/system/
}

FILES:${PN} += "${systemd_unitdir}/system/rave2-pcie-programmer.service ${base_sbindir}/recovery-init"
