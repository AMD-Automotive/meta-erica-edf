#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

"""
versal2_mmi_pcie_check.py

MMI PCIe / GTYP sanity checks on-target using devmem2.

Console:
  - Minimal summary + TAP-ish ok/not ok lines (for human scan + tooling)

Log:
  - Verbose details to /tmp/output/versal2_mmi_pcie_check_<epoch>.txt

Exit code:
  - number of failed checks (0 == pass, >0 == count of failures)
  - 77 == skip (must run as root)
"""

import os
import re
import subprocess
import sys
import time
from typing import Callable, Tuple, List

HEX_RE = re.compile(r"0x[0-9a-fA-F]+")


def devmem2_read(addr: int) -> int:
    proc = subprocess.run(
        ["devmem2", f"0x{addr:X}"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"devmem2 failed at 0x{addr:X} (rc={proc.returncode})\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}\n"
        )

    matches = HEX_RE.findall(proc.stdout)
    if not matches:
        raise RuntimeError(f"Unable to parse devmem2 output for 0x{addr:X}:\n{proc.stdout}")

    # devmem2 prints multiple hex tokens; last is typically the value
    return int(matches[-1], 16)


def read(addr: int, log) -> int:
    val = devmem2_read(addr)
    log.write(f"0x{addr:X} = 0x{val:X}\n")
    return val


def check_any(addr: int, name: str, log) -> bool:
    """
    Always PASS if readable. Use for regs where 0 is a valid 'healthy' value,
    e.g. interrupt status (no pending IRQs).
    """
    try:
        val = read(addr, log)
    except Exception as e:
        log.write(f"ERROR reading {name}: {e}\n")
        return False

    log.write(f"{name} = 0x{val:X} -> PASS (info)\n")
    return True


def check_nonzero(addr: int, name: str, log) -> bool:
    """
    PASS if readable and non-zero.
    """
    try:
        val = read(addr, log)
    except Exception as e:
        log.write(f"ERROR reading {name}: {e}\n")
        return False

    if val == 0:
        log.write(f"{name} = 0 -> FAIL\n")
        return False

    log.write(f"{name} = 0x{val:X} -> PASS\n")
    return True


def check_equals(addr: int, name: str, expected: int, log) -> bool:
    """
    PASS if readable and equals expected.
    """
    try:
        val = read(addr, log)
    except Exception as e:
        log.write(f"ERROR reading {name}: {e}\n")
        return False

    if val != expected:
        log.write(f"{name} = 0x{val:X} -> FAIL (expected 0x{expected:X})\n")
        return False

    log.write(f"{name} = 0x{val:X} -> PASS (expected)\n")
    return True


def main() -> int:
    # ptest skip convention
    if os.geteuid() != 0:
        print("SKIP (must run as root)")
        return 77

    os.makedirs("/tmp/output", exist_ok=True)
    timestamp = int(time.time())
    logfile = f"/tmp/output/versal2_mmi_pcie_check_{timestamp}.txt"

    with open(logfile, "w") as log:
        log.write("Versal2 MMI PCIe Check\n")
        log.write(f"Start time: {time.ctime()}\n\n")

        # Each tuple: (name, addr, check_fn)
        tests: List[Tuple[str, int, Callable[..., bool]]] = [
            ("GT_POWERGOOD", 0xED900008, check_nonzero),

            # INT status 0 is OK (no interrupts pending)
            ("GT_INT_STATUS", 0xED900020, check_any),

            # explicitly require exact expected values for pcie0 reset regs
            ("PCIE0_RESET0", 0xED902008, lambda a, n, l: check_equals(a, n, 0x7FFFFFFF, l)),
            ("PCIE0_RESET1", 0xED902408, lambda a, n, l: check_equals(a, n, 0x7FFFFFFF, l)),

            # Leave pcie1 as nonzero sanity checks 
            ("PCIE1_RESET0", 0xED902808, check_nonzero),
            ("PCIE1_RESET1", 0xED902C08, check_nonzero),

            ("PLL_LOCK0", 0xED9021C4, check_nonzero),
            ("PLL_LOCK1", 0xED9025C4, check_nonzero),
        ]

        total = len(tests)
        pass_count = 0
        fail_count = 0

        print("")
        print("Versal2 MMI PCIe Check")
        print("----------------------")
        print("# Versal2 MMI PCIe Check")
        print(f"1..{total}")

        testnum = 1
        for name, addr, fn in tests:
            ok = fn(addr, name, log)

            if ok:
                print(f"ok {testnum} {name}")
                pass_count += 1
            else:
                print(f"not ok {testnum} {name}")
                fail_count += 1

            testnum += 1

        log.write("\nSummary\n")
        log.write(f"Pass={pass_count}\n")
        log.write(f"Fail={fail_count}\n")

    print("")
    print(f"Pass : {pass_count}")
    print(f"Fail : {fail_count}")
    print(f"Log  : {logfile}")
    print("")

    outcome = "PASS" if fail_count == 0 else "FAIL"
    print(f"=================== test: Versal2 MMI PCIe Check  outcome: {outcome} ==================")

    return fail_count


if __name__ == "__main__":
    sys.exit(main())