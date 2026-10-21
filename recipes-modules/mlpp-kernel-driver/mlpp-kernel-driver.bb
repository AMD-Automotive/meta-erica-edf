# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

DESCRIPTION = "A simple example for pipelining frmbuf_rd, crop, scale, cvc, frmbuf_wr"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

SRC_URI = " \
           file://COPYING \
           file://ml_pre_process_main.c \
           file://ml_pre_process.h \
           file://mlpp_ffc_in.c \
           file://mlpp_outputs.c \
           file://mlpp_svc_in.c \
           file://vpss_helpers.c \
           file://Makefile \
        "

inherit module

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD = "ml_pre_process"

do_compile() {
    unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
    oe_runmake KERNEL_SRC=${STAGING_KERNEL_DIR}
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0644 ml_pre_process.ko ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
}

FILES_${PN} = "${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/ml_pre_process.ko"