# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "RAVE2 MSI-X interrupt UIO trigger daemon"
DESCRIPTION = "Userspace daemon that polls UIO devices for buffer-manager \
interrupts and triggers MSI-X to notify the PCIe host"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://rave2_msix_irq_uio.c"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall \
        -o rave2_msix_irq_uio rave2_msix_irq_uio.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/rave2_msix_irq_uio ${D}${bindir}/rave2_msix_irq_uio
}
