# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC

SUMMARY = "Display port ptest"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI += "file://run-ptest \
            file://displayport.py"

S = "${WORKDIR}"

inherit ptest

# ---- Runtime app ----
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/displayport.py \
        ${D}${bindir}/dp-test
}

FILES:${PN} += "${bindir}/dp-test"

RDEPENDS:${PN} += " \
    python3-core \
"

# ---- ptest ----
do_install_ptest() {
    install -d ${D}${PTEST_PATH}
    install -m 0755 ${WORKDIR}/run-ptest \
        ${D}${PTEST_PATH}/
    install -m 0755 ${WORKDIR}/displayport.py \
        ${D}${PTEST_PATH}/
}

RDEPENDS:${PN}-ptest += " \
    python3-core \
    python3-logging \
    python3-unittest \
    python3-pytest \
"