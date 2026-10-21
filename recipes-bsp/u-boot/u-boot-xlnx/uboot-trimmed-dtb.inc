# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

# Opt-in: build u-boot against a trimmed copy of the machine devicetree
# (produced by the device-tree-uboot recipe) instead of the full machine DTB.
# This shrinks the DTB embedded in the u-boot binary without affecting the
# DTB packaged in BOOT.BIN or shipped to Linux.
#
# Include this from u-boot-xlnx%.bbappend for any machine that opts in.

DEPENDS += "device-tree-uboot"

# Redirect EXT_DTB to the trimmed sibling produced by device-tree-uboot.
# Must match UBOOT_DTB_SUFFIX in device-tree-uboot.bb.
BASE_DTS = "${@os.path.splitext(os.path.basename(d.getVar('CONFIG_DTFILE') or ''))[0] or 'system-top'}-uboot"
