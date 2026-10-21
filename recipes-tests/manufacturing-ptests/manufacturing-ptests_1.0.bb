# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Auto-run all ptests from /root/.profile on ttyAMA0 (manufacturing)"
DESCRIPTION = "Installs /root/.profile to run ptest-runner once per boot on ttyAMA0, plus a wrapper script."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
    file://run-manufacturing-ptests \
"

S = "${WORKDIR}"

RDEPENDS:${PN} = "ptest-runner"

do_install() {
    # Wrapper script
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/run-manufacturing-ptests ${D}${sbindir}/run-manufacturing-ptests
}

FILES:${PN} += "\
    ${sbindir}/run-manufacturing-ptests \
"