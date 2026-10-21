# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SDT_FILENAME_SCR_ATTR ??= "sdt_filename_scr"

BIF_PARTITION_ATTR:append = " ${SDT_FILENAME_SCR_ATTR}"

BIF_PARTITION_ATTR[sdt_filename_scr] ?= "type=raw, load=0x1100000"
BIF_PARTITION_IMAGE[sdt_filename_scr] ?= "${RECIPE_SYSROOT}/boot/sdt_filename.scr"
BIF_PARTITION_ID[sdt_filename_scr] ?= "0x1c000000"