# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC

SUMMARY = "Portable network interface information for Python"
HOMEPAGE = "https://pypi.org/project/sockjs/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=eee30f02e3d2ab919885075b8fa899de"

inherit pypi setuptools3

PYPI_PACKAGE = "sockjs"

SRC_URI[sha256sum] = "57e959a23f20a8d55149d1e5db0b3b870726f2b4ad8166d41bdcf411b36cdf76"

DEPENDS += "python3"
RDEPENDS:${PN} += "python3-core"