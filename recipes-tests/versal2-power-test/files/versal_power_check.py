#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

import os
import re
import subprocess
import sys
import time

HEX_RE = re.compile(r"0x[0-9a-fA-F]+")


def devmem2_read(addr):

    cmd = ["devmem2", f"0x{addr:X}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)

    if proc.returncode != 0:
        raise RuntimeError(f"devmem2 failed for 0x{addr:X}")

    matches = HEX_RE.findall(proc.stdout)

    if not matches:
        raise RuntimeError("Unable to parse devmem2 output")

    return int(matches[-1], 16)


PMC_SYSMON_BASEADDR = 0xF1270000
PMC_SYSMON_SUPPLY0 = PMC_SYSMON_BASEADDR + 0x1040
PMC_SYSMON_SUPPLY0_TH_LOWER = PMC_SYSMON_BASEADDR + 0x1980
PMC_SYSMON_NEW_DATA_FLAG0 = PMC_SYSMON_BASEADDR + 0x1000

SYSMON_THRESH_NOT_PROGRAMMED = 0x20000

# SysMon supply register bit fields (from kernel versal-sysmon driver)
SYSMON_MANTISSA_MASK = 0xFFFF
SYSMON_MODE_MASK = 0x60000
SYSMON_MODE_SHIFT = 17
SYSMON_FMT_MASK = 0x10000


def raw_to_millivolts(raw):
    """Convert a SysMon supply register value to millivolts."""
    mantissa = raw & SYSMON_MANTISSA_MASK
    mode = (raw & SYSMON_MODE_MASK) >> SYSMON_MODE_SHIFT
    fmt = (raw & SYSMON_FMT_MASK) >> 16
    exponent = 16 - mode

    if fmt and (mantissa >> 15):
        return ((~mantissa & SYSMON_MANTISSA_MASK) * -1000) >> exponent
    return (mantissa * 1000) >> exponent


rail_list = [
    (0x4328029, 0x9F, "VCCINT_PMC"),
    (0x432802A, 0x9E, "VCCAUX_PMC"),
    (0x432802B, 0x9D, "VCCINT_PSLP"),
    (0x432802C, 0x9C, "VCCINT_PSFP"),
    (0x432802D, 0x9B, "VCCINT_SOC"),
    (0x432802E, 0x9A, "VCCINT_RAM"),
    (0x432802F, 0x99, "VCCAUX"),
    (0x4328030, 0x98, "VCCINT_PL"),
    (0x4328061, 0x97, "VCCINT_ME"),
]


default_thresholds = {
    "VCCINT_PMC": 0x2547A,
    "VCCAUX_PMC": 0x2B333,
    "VCCINT_PSLP": 0x2547A,
    "VCCINT_PSFP": 0x2547A,
    "VCCINT_SOC": 0x25F5C,
    "VCCINT_RAM": 0x25F5C,
    "VCCAUX": 0x2B333,
    "VCCINT_PL": 0x2547A,
    "VCCINT_ME": 0x2547A,
}


def calc_sysmon_regs(index):

    supply_reg = PMC_SYSMON_SUPPLY0 + (index * 4)
    low_th_reg = PMC_SYSMON_SUPPLY0_TH_LOWER + (index * 4)
    new_data_flag = PMC_SYSMON_NEW_DATA_FLAG0 + ((index // 32) * 4)
    bit_offset = index % 32

    return supply_reg, low_th_reg, new_data_flag, bit_offset


def check_power_rail(node_id, index, rail_name, log):

    supply_reg, low_th_reg, new_data_reg, bit_offset = calc_sysmon_regs(index)

    log.write(f"Checking {rail_name}\n")
    log.write(f"NodeID=0x{node_id:X} Index={index}\n")

    try:

        flag_val = devmem2_read(new_data_reg)
        low_thresh = devmem2_read(low_th_reg)
        rail_voltage = devmem2_read(supply_reg)

        log.write(f"flag=0x{flag_val:X}\n")
        log.write(f"threshold=0x{low_thresh:X}\n")
        log.write(f"voltage=0x{rail_voltage:X}\n")

        voltage_mv = raw_to_millivolts(rail_voltage)
        log.write(f"voltage_decoded={voltage_mv}mV ({voltage_mv / 1000.0:.3f}V)\n")

    except Exception as e:

        log.write(f"READ ERROR: {e}\n")
        return "FAIL", ""

    if low_thresh == SYSMON_THRESH_NOT_PROGRAMMED:

        if rail_name not in default_thresholds:

            log.write("NO THRESHOLD\n")
            return "FAIL", ""

        check_thresh = default_thresholds[rail_name]

        log.write(f"Using default threshold 0x{check_thresh:X}\n")

    else:

        check_thresh = low_thresh

    thresh_mv = raw_to_millivolts(check_thresh)
    log.write(f"threshold_decoded={thresh_mv}mV ({thresh_mv / 1000.0:.3f}V)\n")

    volts_str = f"{voltage_mv / 1000.0:.3f}V (thresh {thresh_mv / 1000.0:.3f}V)"

    if rail_voltage < check_thresh:

        log.write(
            f"FAIL voltage 0x{rail_voltage:X} < threshold 0x{check_thresh:X}\n"
        )

        return "FAIL", volts_str

    margin = rail_voltage - check_thresh

    log.write(f"PASS margin=0x{margin:X}\n")

    return "PASS", volts_str


def main():

    if os.geteuid() != 0:
        print("SKIP (must run as root)")
        return 77

    os.makedirs("/tmp/output", exist_ok=True)

    timestamp = int(time.time())
    logfile = f"/tmp/output/versal2_power_test_{timestamp}.txt"

    log = open(logfile, "w")

    log.write("Versal2 Sysmon Power Rail Test\n")
    log.write(f"Start time: {time.ctime()}\n\n")

    pass_count = 0
    fail_count = 0

    total_tests = len(rail_list)

    print("")
    print("Versal2 Power Rail Test")
    print("-----------------------")

    print("# Versal2 Power Rail Test")
    # TAP header
    print(f"1..{total_tests}")

    testnum = 1

    for node_id, index, rail_name in rail_list:

        result, volts_str = check_power_rail(node_id, index, rail_name, log)

        if result == "PASS":

            pass_count += 1
            print(f"ok {testnum} {rail_name} # {volts_str}")

        else:

            fail_count += 1
            print(f"not ok {testnum} {rail_name} # {volts_str}")

        testnum += 1

    log.write("\nSummary\n")
    log.write(f"Pass={pass_count}\n")
    log.write(f"Fail={fail_count}\n")

    log.close()

    print("")
    print(f"Pass : {pass_count}")
    print(f"Fail : {fail_count}")
    print(f"Log  : {logfile}")
    print("")

    if fail_count == 0:
        outcome = "PASS"
    else:
        outcome = "FAIL"

    print(f"=================== test: Versal2 Power Rail Test  outcome: {outcome} ==================")

    return fail_count


if __name__ == "__main__":
    sys.exit(main())