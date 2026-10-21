# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "DOMu configuration files"
DESCRIPTION = "Some Xen configuration files for DOMu launches"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Specify the source files to include in the recipe
SRC_URI = "file://guest0.cfg \
           file://guest0-launch \
            "

# Define the destination for the files
do_install() {
    install -d ${D}${sysconfdir}/xen  
    install -m 0644 ${WORKDIR}/guest0.cfg ${D}${sysconfdir}/xen/guest0.cfg

    install -d ${D}${base_bindir}
    install -m 0777 ${WORKDIR}/guest0-launch ${D}${base_bindir}/guest0-launch
}

RDEPENDS:${PN} += "bash"
FILES:${PN} += "${sysconfdir}/* ${base_bindir}/*"