# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

DESCRIPTION = "Override SSHD configuration to enable passwordless root login via ssh"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI += "file://ssh-rootlogin.conf"

do_install() {
    install -d ${D}/etc/ssh/sshd_config.d
    install -m 0644 ${WORKDIR}/ssh-rootlogin.conf ${D}/etc/ssh/sshd_config.d/ssh-rootlogin.conf
}

RDEPENDS:${PN} += " openssh"

FILES:${PN} += "/etc/ssh/sshd_config.d/*.conf"