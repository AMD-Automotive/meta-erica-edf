# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# 2026.1 (kernel 6.18) VCU2 al_riscv driver fix.

SRC_URI:append:amdtool-2026_1 = " \
            file://0001-al_riscv-fix-dma_buf-lifetime-bugs-on-client-dma_buf.patch \
"
