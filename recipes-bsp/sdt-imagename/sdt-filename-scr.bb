# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Recipe to create a u-boot script file from the filename of the SDT"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit deploy image-artifact-names

SDT_URI ??= "${@'file://${SYSTEM_DTFILE_DIR}' if d.getVar('SYSTEM_DTFILE_DIR') else ''}"

COMPATIBLE_HOST:xilinx-standalone = "${HOST_SYS}"
PACKAGE_ARCH ?= "${MACHINE_ARCH}"

DEPENDS += "u-boot-mkimage-native"
SYSROOT_DIRS += "/boot"

do_configure() {
    FULL_SDT_PATH=${SDT_URI}
    SDT_FILE_PREFIX=$(echo "${FULL_SDT_PATH##*/}" | awk -F. 'NF{print $1}')
    echo "SDT Filename = ${SDT_FILE_PREFIX}"
    echo "sdt_filename=${SDT_FILE_PREFIX}"> ${S}/sdt_filename.txt
}

do_compile() {
    mkimage -A arm -O linux -T script -C none -n "SDT Filename variable" -d ${S}/sdt_filename.txt ${S}/sdt_filename.scr
}


do_install(){
        install -d ${D}/boot
        install -m 0644 ${S}/sdt_filename.scr ${D}/boot/sdt_filename.scr
}

FILES:${PN} += "/boot/*.scr"
PROVIDES += "sdt_filename_scr"