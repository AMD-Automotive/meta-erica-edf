# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

SUMMARY = "Runtime PL artifacts (edf-pl-overlay.pdi + pl.dtbo) for segmented boot"
DESCRIPTION = "Builds the runtime PL device-tree overlay emitted by the segmented \
gen-machineconf flow, packages it with the PL design PDI for dfx-mgr, and \
deploys compatibility aliases for board programming workflows."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Any machine imported with the segmented flow can produce runtime PL artifacts.
COMPATIBLE_MACHINE = "${@'${MACHINE}' if d.getVar('PL_RUNTIME_LOAD') == '1' else '^$'}"

INHIBIT_DEFAULT_DEPS = "1"

# Lopper derives Linux PL access policy from the SDT A78 address map.
# sdt-artifacts stages the source SDT and PL design PDI into the recipe sysroot.
PDI_PATH_DEPENDS ?= "sdt-artifacts"
DEPENDS = "dtc-native lopper-native ${PDI_PATH_DEPENDS}"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit deploy

# Resolve the `pl.dtsi` emitted by `gen-machineconf -g full` and the hand-authored
# PL-side overrides from the machine's conf/dts dir, wherever that layer lives on
# BBPATH (the same mechanism as device-tree.bbappend's SYSTEM_USER_DTSI pickup).
FILESEXTRAPATHS:prepend := "${@bb.utils.which(d.getVar('BBPATH'), 'conf/dts/%s' % d.getVar('MACHINE'))}:"

PL_OVERLAY_DTSI ?= "pl-overlay-full/pl.dtsi"
PL_OVERRIDES_DTSI ?= ""
PL_OVERRIDES_DTSI:rave2-baseline-seg = "rave2-baseline-seg-pl-overrides.dtsi"
PL_OVERRIDES_DTSI:rave2-ecp-seg = "rave2-ecp-seg-pl-overrides.dtsi"

SRC_URI = " \
    file://${PL_OVERLAY_DTSI} \
    ${@'file://${PL_OVERRIDES_DTSI}' if d.getVar('PL_OVERRIDES_DTSI') else ''} \
    file://pl_domain_policy.py \
"

# The segmented import records the handoff's runtime PDI basename; sdt-artifacts
# stages that PDI under ${datadir}/sdt/${MACHINE}.
PLD_PDI ?= "${RECIPE_SYSROOT}${datadir}/sdt/${MACHINE}/${PLD_PDI_FILE}"
SDT_SYSTEM_DTS ?= "${RECIPE_SYSROOT}${datadir}/sdt/${MACHINE}/system-top.dts"
PL_DOMAIN_PROCESSOR ?= "cortexa78_0"
FW_INSTALL_DIR ?= "${PN}"

S = "${WORKDIR}"

do_compile() {
    if [ ! -f "${SDT_SYSTEM_DTS}" ]; then
        bbfatal "System DTS not found: ${SDT_SYSTEM_DTS}"
    fi

    install -d ${B}/domain-policy
    PL_DOMAIN_PROCESSOR=${PL_DOMAIN_PROCESSOR} \
        LOPPER_DTC_FLAGS="-b 0 -@" \
        lopper --enhanced -A ${WORKDIR} -O ${B}/domain-policy -f \
        ${SDT_SYSTEM_DTS} ${B}/domain-policy/system-top.dts \
        -- pl_domain_policy

    if [ ! -f "${B}/domain-policy/pl-domain-policy.dtsi" ]; then
        bbfatal "PL domain policy was not generated"
    fi

    # Keep the complete module overlay emitted by gen-machineconf. Append the
    # SDT-derived Linux access policy, then board-specific functional bindings.
    sed -E 's/firmware-name = "[^"]+\.pdi";/firmware-name = "${PN}.pdi";/' \
        ${WORKDIR}/${PL_OVERLAY_DTSI} > ${B}/pl-combined.dtsi
    cat ${B}/domain-policy/pl-domain-policy.dtsi >> ${B}/pl-combined.dtsi
    if [ -n "${PL_OVERRIDES_DTSI}" ]; then
        cat ${WORKDIR}/${PL_OVERRIDES_DTSI} >> ${B}/pl-combined.dtsi
    fi

    dtc -@ -I dts -O dtb -o ${B}/pl.dtbo ${B}/pl-combined.dtsi
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware/xilinx/${FW_INSTALL_DIR}
    install -m 0644 ${B}/pl.dtbo \
        ${D}${nonarch_base_libdir}/firmware/xilinx/${FW_INSTALL_DIR}/${PN}.dtbo
    if [ ! -f "${PLD_PDI}" ]; then
        bbfatal "PL design PDI not found: ${PLD_PDI}"
    fi
    install -m 0644 ${PLD_PDI} \
        ${D}${nonarch_base_libdir}/firmware/xilinx/${FW_INSTALL_DIR}/${PN}.pdi
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/pl.dtbo ${DEPLOYDIR}/pl.dtbo
    if [ ! -f "${PLD_PDI}" ]; then
        bbfatal "PL design PDI not found: ${PLD_PDI}"
    fi
    install -m 0644 ${PLD_PDI} ${DEPLOYDIR}/${PN}.pdi
    ln -sf ${PN}.pdi ${DEPLOYDIR}/pl-design.pdi
}
addtask deploy after do_compile before do_build

FILES:${PN} += "${nonarch_base_libdir}/firmware/xilinx/${FW_INSTALL_DIR}"
