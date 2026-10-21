# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

SUMMARY = "Open Hardware Test Framework"
DESCRIPTION = "Framework for building automated hardware tests"
HOMEPAGE = "https://pypi.org/project/openhtf"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a715ed2d9713dc2f3b79804d5ba803a6"

inherit pypi setuptools3

PYPI_PACKAGE = "openhtf"

SRC_URI[sha256sum] = "ff8b2d20563c8ee2f512a5126adee18bc5c0adf7040f75d7b498ee99161b8c6e"

# ------------------------
# Build-time dependencies
# ------------------------
DEPENDS += "\
    python3-setuptools-native \
    python3-wheel-native \
    protobuf-native \
"

# ------------------------
# Runtime dependencies
# ------------------------
RDEPENDS:${PN} += "\
    packagegroup-meta-python3-tests \
    python3-mutablerecords \
    python3-core \
    python3-protobuf \
    python3-attrs \
    python3-psutil \
    python3-pyyaml \
    python3-six \
    python3-typing-extensions \
    python3-inflection \
    python3-future \
    python3-dateutil \
    python3-pytz \
    python3-colorama \
    python3-psutil \
    python3-jsonschema \
    python3-sockjs \
"

FILES:${PN} += "${PYTHON_SITEPACKAGES_DIR}"

PYTHON_COMPATIBILITY = "python3"