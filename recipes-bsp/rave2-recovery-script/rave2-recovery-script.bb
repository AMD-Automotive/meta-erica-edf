# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "U-Boot recovery boot script"
DESCRIPTION = "Creates recovery boot script for RAVE2"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://recovery-boot.cmd"

S = "${WORKDIR}"

inherit deploy

# Prevent SDK inclusion
EXCLUDE_FROM_WORLD = "1"

do_compile() {
    mkimage -A arm64 -O linux -T script -C none -a 0 -e 0 \
        -n "Recovery Boot Script" \
        -d ${WORKDIR}/recovery-boot.cmd ${B}/recovery-boot.scr
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/recovery-boot.scr ${DEPLOYDIR}/recovery-boot.scr
}

addtask deploy after do_compile before do_build

# Don't create any packages
PACKAGES = ""