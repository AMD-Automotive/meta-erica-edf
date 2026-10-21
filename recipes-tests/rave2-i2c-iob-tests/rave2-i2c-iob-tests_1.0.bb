# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "RAVE2 I2C IOB device presence test (ptest)"
DESCRIPTION = "Checks that I2C devices 0x27, 0x28, 0x2b are present on at both I2C buses using i2cdetect."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://run-ptest \
    file://rave2_i2c_iob_tests.py \
"

S = "${WORKDIR}"

inherit ptest

ALLOW_EMPTY:${PN} = "1"
RDEPENDS:${PN} += "${PN}-ptest"

RDEPENDS:${PN}-ptest += "\
    python3-core \
    i2c-tools \
"

do_install() {
    :
}

do_install_ptest() {
    install -d ${D}${PTEST_PATH}
    install -m 0755 ${WORKDIR}/run-ptest ${D}${PTEST_PATH}/run-ptest
    install -m 0755 ${WORKDIR}/rave2_i2c_iob_tests.py ${D}${PTEST_PATH}/rave2_i2c_iob_tests.py
}

FILES:${PN}-ptest += "${PTEST_PATH}"
