# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC

SUMMARY = "Simple mutable record types for Python"
HOMEPAGE = "https://pypi.org/project/mutablerecords/"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://setup.py;beginline=24;endline=24;md5=cb6c9db1b2f3c216f35d55cefd6b3697"

inherit pypi setuptools3

PYPI_PACKAGE = "mutablerecords"

SRC_URI[sha256sum] = "be54b2688c87126866fc52414bd1f10d6e2943f79bf92665fec95399252108b4"

DEPENDS += "python3"
RDEPENDS:${PN} += "python3-core python3-six"

FILES:${PN} += "${PYTHON_SITEPACKAGES_DIR}"

PYTHON_COMPATIBILITY = "python3"