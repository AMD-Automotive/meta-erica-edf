# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# Modifications Copyright © 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC
# Adapted from the meta-openembedded recipe python3-tornado_6.4.2.bb, which is
# MIT under that layer's COPYING.MIT and carries no per-file copyright notice.

SUMMARY = "Tornado is a Python web framework and asynchronous networking library"
HOMEPAGE = "https://www.tornadoweb.org/"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57"

inherit pypi python_setuptools_build_meta

PYPI_PACKAGE = "tornado"

SRC_URI[sha256sum] = "72291fa6e6bc84e626589f1c29d90a5a6d593ef5ae68052ee2ef000dfd273dee"

DEPENDS += "python3"
RDEPENDS:${PN} += "python3-core python3-asyncio"