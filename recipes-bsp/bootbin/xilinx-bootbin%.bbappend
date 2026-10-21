# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT, KC, JC
# Maintainer: LT

include sdt-filename-scr.inc

include ${@'vek385-isp-hello-world-r52-1-baremetal.inc' if ((d.getVar('MACHINE') in ['versal-2ve-2vm-vek385-sdt-seg', 'vek385-isp']) and (d.getVar('BB_CURRENT_MC') != 'xilinx-image-recovery')) else ''}

include ${@'vek385-isp-firmware.inc' if ((d.getVar('MACHINE') == 'vek385-isp') and (d.getVar('BB_CURRENT_MC') != 'xilinx-image-recovery')) else ''}

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

do_compile:prepend:versal-2ve-2vm-generic () {

        if [ "${BOOTBIN_NO_PL}" != "1" ]; then
                echo "Appending PL configuration to bootgen bif"

                cp -v ${PKG_CONFIG_SYSROOT_DIR}/boot/*.rcdo ${B}
                cp -v ${PKG_CONFIG_SYSROOT_DIR}/boot/*.rnpi ${B}
                cp -v ${PKG_CONFIG_SYSROOT_DIR}/boot/pl-design.pdi ${B}
                cp -v ${PKG_CONFIG_SYSROOT_DIR}/boot/pl.bif ${B}

                mv bootgen.bif bootgen_nopl.bif

                # remove top level of hierarchy off of pl bif. Tab align it to look neat
                awk '/image/{found=1} found' pl.bif | sed '$s/}$//g' > pl_stripped.bif
                sed -i 's/ /\t/ ' pl_stripped.bif

                # Strip last bracket off of bootgen.bif
                head -n -1 bootgen_nopl.bif > bootgen.bif

                # Add stripped pl bif to bootgen bif
                cat pl_stripped.bif >> bootgen.bif

                rm -rf pl_stripped.bif

                # Closing bracket
                echo "}" >> bootgen.bif
        else
                echo "BOOTBIN_NO_PL=1, skipping PL bootgen modifications"
        fi
}

rave2_patch_base_design_pdi() {
        echo "System DT file directory: ${SYSTEM_DTFILE_DIR}"

        # Find first file matching *_boot.bif
        boot_bif_path="$(find "${SYSTEM_DTFILE_DIR}" -type f -name "*_boot.bif" -print -quit)"
        if [[ -z "${boot_bif_path}" ]]; then
                bbfatal "No '*_boot.bif' found under SYSTEM_DTFILE_DIR='${SYSTEM_DTFILE_DIR}'"
        fi

        # Extract directory and filename
        PDI_DIR=$(dirname "${boot_bif_path}")
        BOOT_BIF=$(basename "${boot_bif_path}")

        echo "PDI_DIR: ${PDI_DIR}"
        echo "BOOT_BIF: ${BOOT_BIF}"

        (
                cd "${PDI_DIR}"
                CDO_FILE="gen_files/fpd_data.cdo"

                if [ ! -f "${CDO_FILE}" ]; then
                        bbfatal "Expected file not found: ${PWD}/${CDO_FILE}"
                fi

                matches="$(grep -n "^write 0xedc2025c" "${CDO_FILE}" || true)"

                if [ -z "${matches}" ]; then
                        bbwarn "No lines found starting with 'write 0xedc2025c' in ${CDO_FILE}"
                        echo "No lines found starting with 'write 0xedc2025c' in ${CDO_FILE}"
                else
                        # Replace all writes to edc2025c with the value 0
                        echo "Patching ${CDO_FILE}"
                        sed -i -E 's/^write 0xedc2025c.*/write 0xedc2025c 0x0/' "${CDO_FILE}"
                fi

                # Rename the original pdi so we dont get confused
                cp ${B}/base-design.pdi ${B}/unpatched-base-design.pdi

                # Build patched pdi and deploy to build directory
                echo "Running bootgen on ${BOOT_BIF}"
                bootgen -arch versal_2ve_2vm -image ${BOOT_BIF} -w -o base-design.pdi
                cp base-design.pdi ${B}
        )
}


# rave2-ecp-2026-1: force the PCIe endpoint BAR layout and hide SR-IOV.
#
# WHY THIS IS NEEDED
#   ps_wizard expresses "BAR disabled" as the ABSENCE of a CDO write rather than
#   as a write of zero. The BAR mask registers reset to a non-zero (enabled)
#   value, so a BAR turned off in the Vivado project still enumerates on the
#   host. Setting CONFIG.MMI_CONFIG(PCIE0_PF0_BAR4_EN) {0} produces no BAR4 mask
#   write at all, and the same is true of BAR5 and of the SR-IOV capability.
#
#   The generated fpd_data.cdo proves both halves of this:
#     - it writes `write 0x<mask>+0x014 0x0` for BAR1, and BAR1 is the one BAR
#       that correctly reads back mask=0 and does not enumerate;
#     - it never writes +0x020 or +0x024 (BAR4/BAR5) for any PF, and those are
#       exactly the BARs that appear unwanted -- 4 KB and 64 KB of memory, or a
#       256-byte I/O BAR on PF3 where the per-function default differs.
#
#   Result without this patch: four extra BARs per function, an SR-IOV
#   capability advertising 8 VFs per PF (32 total) with six sized VF BARs each,
#   none of which the design implements. That wastes host address space and
#   misleads a hypervisor, which inspects SR-IOV when assigning a function.
#
# ADDRESSES (all verified against silicon, not inferred)
#   PF DBI config space   0xED800000 + pf*0x1000
#   PF BAR mask window    0xED808000 + pf*0x1000   (+0x10..+0x24 = BAR0..BAR5)
#
#   The extended capability chain differs BETWEEN functions, so the capability
#   that must be re-pointed to hide SR-IOV is NOT the same offset on every PF.
#   Read live from the endpoint's own DBI over the Versal serial console:
#
#     PF0  +0x1d4 Data Link Feature -> 0x1f8 (SR-IOV) -> 0x238 (ACS)
#     PF1  +0x168 ARI               -> 0x1f8 (SR-IOV) -> 0x238 (ACS)
#     PF2  +0x168 ARI               -> 0x1f8 (SR-IOV) -> 0x238 (ACS)
#     PF3  +0x168 ARI               -> 0x1f8 (SR-IOV) -> 0x238 (ACS)
#
#   Applying PF0's offset to PF1-3 would rewrite a capability that does not
#   exist there (+0x1d4 reads 0x00000000 on PF1-3) and corrupt the chain.
#
# WHAT IT DOES
#   1. Appends `write <mask>+0x020 0` and `+0x024 0` for each PF, disabling
#      BAR4/BAR5 the same way the tool already disables BAR1.
#   2. Re-points each PF's SR-IOV predecessor straight at ACS (0x238), so the
#      SR-IOV capability is skipped when software walks the chain and the VF
#      BARs are never enumerated. Only the next-pointer field is touched
#      (mask 0xfff00000); capability ID and version are left alone.
#
# WHERE THE WRITES MUST GO -- THIS IS NOT A SIMPLE APPEND
#   fpd_data.cdo configures the PCIe controller TWICE. The first pass is at
#   top level; the second lives inside `proc 0x1`, a procedure the CDO never
#   calls itself -- the PLM invokes it as the MMI_PCIE_REPROG phase, after the
#   controller has been reset.
#
#   Only the second pass survives. Proof: the top-level pass already writes
#   `mask_write 0xed8001d4 0xffff0000 0x2381002a`, which points PF0's Data Link
#   Feature at ACS and would hide SR-IOV -- yet the running endpoint reads back
#   0x1f81002a (next = 0x1f8, SR-IOV linked). The reset between the two passes
#   discards the first, and proc 0x1 never rewrites +0x1d4, so it reverts to its
#   hardware default.
#
#   Consequently writes appended to the END of the file are dead code: they land
#   outside proc 0x1 and after the MMI_FINISH marker. An earlier version of this
#   patch did exactly that, built and flashed cleanly, and changed nothing.
#   The writes are therefore spliced in just before the `end` that closes
#   proc 0x1, located via the unique marker 0x65 "MMI_PCIE_REPROG" that follows
#   it. The caller runs bootgen afterwards to rebuild the PDI.
#
# VERIFYING THE RESULT
#   Do NOT try to read back the mask registers. The 0xED808000 window is the
#   DWC dbi_cs2 shadow: writes there set the BAR mask, but reads return the BAR
#   value instead (0xed808020 reads as PF0's BAR4 address). Confirm from the
#   host side instead -- the BAR should stop enumerating, and
#   /sys/bus/pci/devices/*/sriov_totalvfs should disappear. Also check that the
#   capabilities AFTER SR-IOV survive (ACS, LTR, PASID and the VSECs, including
#   the eDMA VSEC dw-edma depends on); a bad unlink silently truncates the chain.
rave2_ecp_patch_pcie_bars() {
        boot_bif_path="$(find "${SYSTEM_DTFILE_DIR}" -type f -name "*_boot.bif" -print -quit)"
        if [[ -z "${boot_bif_path}" ]]; then
                bbfatal "pcie-bars: no '*_boot.bif' found under SYSTEM_DTFILE_DIR"
        fi
        PDI_DIR=$(dirname "${boot_bif_path}")
        CDO_FILE="${PDI_DIR}/gen_files/fpd_data.cdo"

        if [ ! -f "${CDO_FILE}" ]; then
                bbfatal "pcie-bars: expected file not found: ${CDO_FILE}"
        fi

        if grep -q "ERICA PCIe BAR/SR-IOV override" "${CDO_FILE}"; then
                echo "pcie-bars: override already present in $(basename ${CDO_FILE}), skipping"
                return 0
        fi

        # Sanity-check the tool still emits what we expect. If BAR1's disable
        # write has gone, ps_wizard's codegen has changed and these offsets
        # deserve re-verification before being trusted. Fatal rather than a
        # warning: silently producing an unpatched boot.bin costs a flash cycle
        # and looks exactly like the patch having no effect.
        if ! grep -q "^write 0xed808014 0$" "${CDO_FILE}"; then
                bbfatal "pcie-bars: expected BAR1 disable 'write 0xed808014 0' not found in ${CDO_FILE} -- ps_wizard output has changed; re-verify the BAR mask offsets before trusting this patch"
        fi

        # Locate the end of proc 0x1 (see comment block above). The marker is
        # unique in the file and sits immediately after the proc's closing
        # 'end'; the writes go in just before that 'end' so they execute last
        # within MMI_PCIE_REPROG.
        anchor_ln="$(grep -n '^[[:space:]]*marker 0x65 "MMI_PCIE_REPROG"$' "${CDO_FILE}" | cut -d: -f1)"
        if [ -z "${anchor_ln}" ] || [ "$(echo "${anchor_ln}" | wc -l)" -ne 1 ]; then
                bbfatal "pcie-bars: expected exactly one 'marker 0x65 \"MMI_PCIE_REPROG\"' in ${CDO_FILE}, found '${anchor_ln}'"
        fi

        # NOTE: use expr, NOT shell arithmetic expansion. BitBake's parser
        # rejects arithmetic expansion in shell functions and fails the whole
        # recipe parse with a NotImplementedError, which also takes down the
        # unrelated meta-xilinx xilinx-bootbin.bb this bbappend attaches to.
        # The same applies to comments -- function bodies are expanded in full,
        # so the offending token must not appear here either.
        end_ln=$(expr "${anchor_ln}" - 1)
        head_ln=$(expr "${end_ln}" - 1)

        if ! sed -n "${end_ln}p" "${CDO_FILE}" | grep -qE '^[[:space:]]*end[[:space:]]*$'; then
                bbfatal "pcie-bars: line ${end_ln} of ${CDO_FILE} is not the 'end' closing proc 0x1 -- CDO structure has changed, refusing to patch"
        fi

        echo "pcie-bars: injecting BAR4/BAR5 disable and SR-IOV unlink into proc 0x1 at line ${end_ln} of $(basename ${CDO_FILE})"

        # Spliced in a single pass with no intermediate file. An earlier version
        # staged the payload under WORKDIR, which expands to nothing in this
        # context -- CI showed it writing and reading "//pcie-override.cdo".
        {
                head -n "${head_ln}" "${CDO_FILE}"
                cat <<-'EOF'
	    # ---- ERICA PCIe BAR/SR-IOV override (injected by xilinx-bootbin bbappend) ----
	    # Disable BAR4/BAR5 on every PF. ps_wizard omits these writes entirely, so
	    # the mask registers keep a non-zero reset value and the BARs enumerate
	    # despite being disabled in the Vivado project. Mirrors its BAR1 handling.
	    write 0xed808020 0
	    write 0xed808024 0
	    write 0xed809020 0
	    write 0xed809024 0
	    write 0xed80a020 0
	    write 0xed80a024 0
	    write 0xed80b020 0
	    write 0xed80b024 0
	    # Hide the SR-IOV capability (ext cap 0x0010 at +0x1f8) by pointing each
	    # PF's preceding capability directly at ACS (0x238). The predecessor is at
	    # a DIFFERENT offset per function: PF0 reaches SR-IOV from Data Link
	    # Feature (+0x1d4), PF1-3 from ARI (+0x168). PF1-3 have no capability at
	    # +0x1d4 at all, so applying PF0's offset uniformly would corrupt the
	    # chain. Only the next-pointer field is written (mask 0xfff00000).
	    mask_write 0xed8001d4 0xfff00000 0x23800000
	    mask_write 0xed801168 0xfff00000 0x23800000
	    mask_write 0xed802168 0xfff00000 0x23800000
	    mask_write 0xed803168 0xfff00000 0x23800000
	    # ---- end ERICA PCIe BAR/SR-IOV override ----
	EOF
                tail -n "+${end_ln}" "${CDO_FILE}"
        } > "${CDO_FILE}.new"
        mv "${CDO_FILE}.new" "${CDO_FILE}"
}

# rave2_imx623_stage_gen_files_cdo, vek385_patch_ipi_tz_pdi, and the sentry
# do_compile overrides that call them moved to meta-erica-sentry's
# xilinx-bootbin%.bbappend, along with FPD_FEATURE/FPD_TOPOLOGY_CDO and the
# board-topology.cdo SRC_URI.

# FPD power-down/resume: inject the board-topology CDO into the base-PDI boot
# (sentry-only; see meta-erica-sentry's xilinx-bootbin%.bbappend for the
# board-topology.cdo SRC_URI, the FPD_FEATURE/FPD_TOPOLOGY_CDO defaults, and
# the vek385_*/rave2_imx623_stage_gen_files_cdo helpers + do_compile overrides
# that call this one).

do_compile:prepend:rave2-baseline() {
        rave2_patch_base_design_pdi
}

do_compile:prepend:rave2-baseline-seg() {
        rave2_patch_base_design_pdi
}

do_compile:prepend:rave2-ecp() {
        rave2_patch_base_design_pdi
}

# rave2-ecp-2026-1 only. The BAR/SR-IOV override must run BEFORE
# rave2_patch_base_design_pdi, because that function is what invokes bootgen --
# edits made after it would never reach the PDI.
do_compile:prepend:rave2-ecp-2026-1() {
        rave2_ecp_patch_pcie_bars
        rave2_patch_base_design_pdi
}

# rave2-ecp-seg is an independent (segmented) machine: it does NOT carry
# 'rave2-ecp' in MACHINEOVERRIDES, so the :rave2-ecp override above does not
# fire for it. It still uses the same patched boot PDI, so reflect it here.
do_compile:prepend:rave2-ecp-seg() {
        rave2_patch_base_design_pdi
}

do_compile:prepend:rave2-recovery() {
        rave2_patch_base_design_pdi
}
