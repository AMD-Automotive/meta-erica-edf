#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

devmem2 0xb0140004 w 0xf
devmem2 0xb0140000 w 0x0
i2cset -y 0 0x28 0x1 0x10
sleep 2
i2cset -y 0 0x28 0x1 0x1f
sleep 1
devmem2 0xb0140000 w 0x10

