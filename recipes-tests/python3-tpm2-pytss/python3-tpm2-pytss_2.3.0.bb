# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# Modifications Copyright © 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC
# Adapted from the meta-security recipe python3-tpm2-pytss_2.1.0.bb, which is
# MIT under that layer's COPYING.MIT and carries no per-file copyright notice.

DESCRIPTION = "TPM2 TSS Python bindings for Enhanced System API (ESYS), Feature API (FAPI), Marshaling (MU), TCTI Loader (TCTILdr), TCTIs, policy, and RC Decoding (rcdecode) libraries"
HOMEPAGE = "https://github.com/tpm2-software/tpm2-pytss"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=500b2e742befc3da00684d8a1d5fd9da"

SRC_URI[sha256sum] = "20071129379656f5f3c3bc16d364612672b147d81191fb4eb9f9ff9fbee48410"

PYPI_PACKAGE = "tpm2-pytss"

DEPENDS = "python3-pkgconfig-native python3-pycparser-native python3-asn1crypto-native"
DEPENDS:append = " python3-cryptography-native tpm2-tss" 

inherit autotools pkgconfig pypi setuptools3_legacy

RDEPENDS:${PN} = "libtss2"
