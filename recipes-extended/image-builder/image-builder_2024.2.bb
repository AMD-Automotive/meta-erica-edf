# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Xen Image Builder"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d2794c0df5b907fdace235a619d80314"

SRC_URI = "git://gitlab.com/xen-project/imagebuilder.git;protocol=https;branch=master"
SRCREV = "cce937bf98ee3f97fe3bda798326b992189c32d4"

S = "${WORKDIR}/git"

inherit deploy

DEPENDS += " \
    u-boot-mkimage-native \
    "

RDEPENDS:${PN} += " \
    bash \
    file \
    "

do_install () {
    install -d ${D}${bindir}
    chmod -R 777 ${S}/scripts/*
    install -m 0755 ${S}/scripts/* ${D}${bindir}
    ln -sf ${D}${bindir}/uboot-script-gen uboot-script-gen
}

do_deploy() {
	:
}

do_deploy:class-native() {
    install -d ${DEPLOYDIR}
    install -m 0755 ${S}/scripts/uboot-script-gen ${DEPLOYDIR}/
}

addtask do_deploy after do_install

BBCLASSEXTEND = "native nativesdk"
