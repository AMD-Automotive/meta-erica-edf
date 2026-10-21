# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "PMOD GPIO loopback ptest"
DESCRIPTION = "Ptest that validates PMOD GPIO pairs via loopback harness on gpiochip0 (0xb0130000)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://pmod_gpio_test.py \
    file://run-ptest \
"

S = "${WORKDIR}"

inherit ptest

ALLOW_EMPTY:${PN} = "1"
RDEPENDS:${PN} += "${PN}-ptest"

RDEPENDS:${PN}-ptest += "\
    python3-core \
    python3-gpiod \
"

do_install() {
    :
}

do_install_ptest() {
    install -d ${D}${PTEST_PATH}

    install -m 0755 ${WORKDIR}/run-ptest \
        ${D}${PTEST_PATH}/run-ptest

    install -m 0755 ${WORKDIR}/pmod_gpio_test.py \
        ${D}${PTEST_PATH}/pmod_gpio_test.py
}

FILES:${PN}-ptest += "${PTEST_PATH}"
