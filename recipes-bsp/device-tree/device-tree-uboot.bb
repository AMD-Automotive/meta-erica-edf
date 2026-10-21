# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Trimmed devicetree for embedding in u-boot"
DESCRIPTION = "\
Builds a slimmed-down copy of the machine devicetree (the one produced by the \
'device-tree' recipe from CONFIG_DTFILE) with selected nodes removed, for use \
as u-boot's EXT_DTB. This only affects the DTB embedded in the u-boot binary; \
the DTB packaged into BOOT.BIN and shipped as /boot/system.dtb is unchanged.\
"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit nopackages

DEPENDS += "device-tree u-boot-tools-xlnx-native"

# Restrict to AMD Versal-family machines that follow the meta-xilinx/meta-amd
# device-tree convention (CONFIG_DTFILE -> boot/devicetree/<base>.dtb).
# COMPATIBLE_MACHINE is matched against MACHINE, so we use SoC-family
# overrides (which extend MACHINEOVERRIDES) to whitelist Versal variants.
COMPATIBLE_MACHINE = "^$"
COMPATIBLE_MACHINE:versal = ".*"
COMPATIBLE_MACHINE:versal-net = ".*"
COMPATIBLE_MACHINE:versal-2ve-2vm = ".*"

PACKAGE_ARCH = "${MACHINE_ARCH}"

# Base name (no extension) of the machine DTB produced by the 'device-tree'
# recipe. Defaults match meta-xilinx-core's BASE_DTS computation.
BASE_DTS ?= "${@os.path.splitext(os.path.basename(d.getVar('CONFIG_DTFILE') or ''))[0] or 'system-top'}"

# Suffix appended to BASE_DTS for the trimmed sibling DTB.
UBOOT_DTB_SUFFIX ?= "-uboot"

# Space-separated list of node paths to strip from the u-boot DTB.
# Override per-machine to remove additional fat nodes if needed.
UBOOT_DTB_REMOVE_NODES ?= "/amba_pl"

DTB_PATH = "boot/devicetree"
DTB_IN  = "${BASE_DTS}.dtb"
DTB_OUT = "${BASE_DTS}${UBOOT_DTB_SUFFIX}.dtb"

do_install() {
    install -d ${D}/${DTB_PATH}
    src="${RECIPE_SYSROOT}/${DTB_PATH}/${DTB_IN}"
    if [ ! -f "$src" ]; then
        bbfatal "Missing input DTB: $src (is CONFIG_DTFILE/BASE_DTS set correctly?)"
    fi
    install -m 0644 "$src" ${D}/${DTB_PATH}/${DTB_OUT}
    for node in ${UBOOT_DTB_REMOVE_NODES}; do
        bbnote "Stripping node ${node} from ${DTB_OUT}"
        fdtput -r ${D}/${DTB_PATH}/${DTB_OUT} ${node} || \
            bbwarn "fdtput failed to remove ${node} (node may not exist)"
    done
}

SYSROOT_DIRS += "/${DTB_PATH}"
