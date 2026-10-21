# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Ptest: UFS storage health, status and performance"
DESCRIPTION = "Read-only UFS validation: discovers the controller and LUNs via \
sysfs, checks health/lifetime registers, verifies HS link mode, and runs fio \
sequential + random read benchmarks with bandwidth reporting."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://ufs_check.py \
    file://run-ptest \
"
ALLOW_EMPTY:${PN} = "1"
S = "${WORKDIR}"

inherit ptest python3native

RDEPENDS:${PN}-ptest += "\
    python3-core \
    python3-json \
    fio \
    sg3-utils \
"

do_install() {
    :
}

do_install_ptest() {
    install -d ${D}${PTEST_PATH}
    install -m 0755 ${WORKDIR}/run-ptest ${D}${PTEST_PATH}/run-ptest
    install -m 0755 ${WORKDIR}/ufs_check.py ${D}${PTEST_PATH}/ufs_check.py
}

FILES:${PN}-ptest += "${PTEST_PATH}"
