# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW

SUMMARY = "MDB5 PCIe FLR completion acknowledge daemon"
DESCRIPTION = "The Versal Gen 2 MMI PCIe controller does not complete a \
Function Level Reset on its own: it latches flr_pf_active for the function \
being reset and holds that function in reset until software writes the \
matching app_flr_pf_done bit in FLR_COMPLETION_CTRL0. That register resets \
to zero, so without a handler every FLR hangs permanently - the function \
stops answering configuration reads and only a power cycle recovers it, \
which blocks PCIe passthrough because the hypervisor resets each function \
before assigning it. This daemon supplies the missing acknowledgement."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://mdb5-flr-ack \
           file://mdb5-flr-ack.service \
           "

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "mdb5-flr-ack.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

# Module ownership confirmed against the running target's package database:
# argparse -> python3-core, ctypes -> python3-ctypes, mmap -> python3-mmap.
# ctypes is used deliberately in place of mmap slicing so that register
# accesses are genuine aligned 32-bit loads and stores.
RDEPENDS:${PN} = "python3-core python3-ctypes python3-mmap"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/mdb5-flr-ack ${D}${sbindir}/mdb5-flr-ack

    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/mdb5-flr-ack.service ${D}${systemd_unitdir}/system/
}

FILES:${PN} += "${systemd_unitdir}/system/mdb5-flr-ack.service"
