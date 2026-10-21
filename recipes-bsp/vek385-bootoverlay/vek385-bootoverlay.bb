# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Recipe to create a u-boot script file for boot overlays"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# inherit deploy image-artifact-names

SRC_URI = "file://VEK385.txt"
S = "${WORKDIR}"

DEPENDS += "u-boot-mkimage-native"
SYSROOT_DIRS += "/boot"



do_compile() {
    mkimage -A arm -O linux -T script -C none -n "VEK385 DT Overlay logic" -d ${S}/VEK385.txt ${S}/VEK385.scr
}


do_install(){
        install -d ${D}/boot
        install -m 0644 ${S}/VEK385.scr ${D}/boot/VEK385.scr
        install -m 0644 ${S}/VEK385.txt ${D}/boot/VEK385.txt
}

FILES:${PN} += "/boot/VEK385.scr /boot/VEK385.txt"
