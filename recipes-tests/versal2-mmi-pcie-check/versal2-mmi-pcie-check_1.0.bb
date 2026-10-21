# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Versal2 MMI PCIe register check"
DESCRIPTION = "Ptest that validates MMI PCIe GTYP/PIPE status registers"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://versal2_mmi_pcie_check.py \
    file://run-ptest \
"

S = "${WORKDIR}"

inherit ptest

ALLOW_EMPTY:${PN} = "1"
RDEPENDS:${PN} += "${PN}-ptest"

RDEPENDS:${PN}-ptest += "\
    python3-core \
    devmem2 \
"

do_install() {
    :
}

do_install_ptest() {

    install -d ${D}${PTEST_PATH}

    install -m 0755 ${WORKDIR}/run-ptest \
        ${D}${PTEST_PATH}/run-ptest

    install -m 0755 ${WORKDIR}/versal2_mmi_pcie_check.py \
        ${D}${PTEST_PATH}/versal2_mmi_pcie_check.py
}

FILES:${PN}-ptest += "${PTEST_PATH}"