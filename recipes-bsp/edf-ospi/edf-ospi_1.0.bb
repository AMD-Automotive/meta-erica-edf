# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "OSPI image for AMD Embedded Development Framework"
DESCRIPTION = "OSPI image for AMD Embedded Development Framework with image selector, capsule metadata and boot.bin"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit erica-edf-spi-image

COMPATIBLE_MACHINE = "^$"

PACKAGE_ARCH = "${MACHINE_ARCH}"
