#!/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

#GPIO for PWRDWN
devmem2 0xb0150004 w 0xf
devmem2 0xb0150000 w 0x0

#Power over Coax off-on
i2cset -y 1 0x28 0x1 0x10
sleep 2
i2cset -y 1 0x28 0x1 0x1f
sleep 1

devmem2 0xb0150000 w 0x10
