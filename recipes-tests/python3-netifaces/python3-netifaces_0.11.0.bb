# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC

SUMMARY = "Portable network interface information for Python"
HOMEPAGE = "https://pypi.org/project/netifaces/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=e4677613c25bf3673bfee98c0cc52202"

inherit pypi

PYPI_PACKAGE = "netifaces"

SRC_URI[sha256sum] = "043a79146eb2907edf439899f262b3dfe41717d34124298ed281139a8b93ca32"

DEPENDS += "python3"
RDEPENDS:${PN} += "python3-core"