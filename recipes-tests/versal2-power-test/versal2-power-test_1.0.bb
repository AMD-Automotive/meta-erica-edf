# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Ptest: SysMon power rail check using devmem2"
DESCRIPTION = "Runs a SysMon power rail validation on-target using devmem2 and fails if any rail is below threshold."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://versal_power_check.py \
    file://run-ptest \
"
ALLOW_EMPTY:${PN} = "1"
S = "${WORKDIR}"

inherit ptest python3native

# Needs python3 to run the test, and devmem2 to read /dev/mem.
RDEPENDS:${PN}-ptest += "\
    python3-core \
    devmem2 \
"

do_install() {
    :
}

do_install_ptest() {
    install -d ${D}${PTEST_PATH}
    install -m 0755 ${WORKDIR}/run-ptest ${D}${PTEST_PATH}/run-ptest
    install -m 0755 ${WORKDIR}/versal_power_check.py ${D}${PTEST_PATH}/versal_power_check.py
}

# Ensure ptest package is produced
FILES:${PN}-ptest += "${PTEST_PATH}"