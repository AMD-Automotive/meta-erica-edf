#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

"""
rave2_i2c_iob_tests.py

Requirement (strict):
    - IOB controller b0170000 must show: 27, 28, 2b
    - IOB controller b0180000 must show: 27, 28, 2b

FAIL if:
  - either bus scan fails (bus missing / permission / error), OR
  - either bus is missing any required address

Logging:
  /tmp/output/rave2_i2c_iob_tests_<epoch>.txt

Exit code:
  - 0 pass
  - N number of missing device instances across both busses (each missing addr counts)
    + if a bus scan fails, that bus counts as missing all required (3)
  - 77 skip (not root)
"""

import os
import subprocess
import sys
import time
from typing import Set, Tuple


REQUIRED = {"27", "28", "2b"}  # lowercase hex without 0x prefix
IOB_CONTROLLERS = ("b0170000", "b0180000")


def resolve_iob_busses():
    aliases = "/proc/device-tree/aliases"
    resolved = []

    for controller in IOB_CONTROLLERS:
        bus = None
        for alias in os.listdir(aliases):
            if not alias.startswith("i2c") or not alias[3:].isdigit():
                continue
            with open(os.path.join(aliases, alias), "rb") as alias_file:
                target = alias_file.read().rstrip(b"\0").decode()
            if target.endswith(f"i2c@{controller}"):
                bus = int(alias[3:])
                break
        if bus is None:
            raise RuntimeError(f"No I2C alias found for controller {controller}")
        resolved.append(bus)

    return resolved


def run_i2cdetect(bus: int) -> Tuple[int, str, str]:
    proc = subprocess.run(
        ["i2cdetect", "-y", str(bus)],
        capture_output=True,
        text=True,
    )
    return proc.returncode, proc.stdout, proc.stderr


def parse_found_addrs(i2cdetect_out: str) -> Set[str]:
    """
    Parse the classic i2cdetect grid and return found addresses as strings like "27".
    """
    found = set()
    for line in i2cdetect_out.splitlines():
        line = line.strip().lower()
        if not line or ":" not in line:
            continue

        # Example: "20: -- -- -- -- -- -- -- 27 28 -- -- 2b -- -- -- --"
        parts = line.split()
        for tok in parts[1:]:
            tok = tok.strip()
            if len(tok) == 2 and all(c in "0123456789abcdef" for c in tok):
                found.add(tok)
    return found


def main() -> int:
    if os.geteuid() != 0:
        print("SKIP (must run as root)")
        return 77

    os.makedirs("/tmp/output", exist_ok=True)
    ts = int(time.time())
    logfile = f"/tmp/output/rave2_i2c_iob_tests_{ts}.txt"

    total_missing_instances = 0
    per_bus_missing = {}
    busses = resolve_iob_busses()

    with open(logfile, "w") as log:
        log.write("RAVE2 I2C IOB Tests (strict)\n")
        log.write(f"Start time: {time.ctime()}\n")
        log.write(f"Required per bus: {sorted(REQUIRED)}\n\n")

        for bus in busses:
            rc, out, err = run_i2cdetect(bus)

            log.write(f"=== i2cdetect -y {bus} (rc={rc}) ===\n")
            if out:
                log.write(out)
            if err:
                log.write("\n[stderr]\n")
                log.write(err)
            log.write("\n")

            if rc != 0:
                # bus missing/inaccessible: count as missing all required
                missing = set(sorted(REQUIRED))
                per_bus_missing[bus] = missing
                total_missing_instances += len(missing)
                log.write(f"Bus {bus}: SCAN FAILED -> missing all {sorted(missing)}\n\n")
                continue

            found = parse_found_addrs(out)
            missing = set(sorted(REQUIRED - found))
            per_bus_missing[bus] = missing
            total_missing_instances += len(missing)

            log.write(f"Bus {bus}: Found={sorted(found)}\n")
            log.write(f"Bus {bus}: Missing={sorted(missing)}\n\n")

    # Console output (minimal)
    print("")
    print("RAVE2 I2C IOB Tests")
    print("-------------------")
    print(f"Log  : {logfile}")

    if total_missing_instances == 0:
        for bus in busses:
            print(f"Bus{bus} : OK (0x27 0x28 0x2b)")
        print("")
        print("=================== test: RAVE2 I2C IOB Tests  outcome: PASS ==================")
        return 0

    # Fail: show what's missing per bus
    for bus in busses:
        missing = per_bus_missing.get(bus, set(REQUIRED))
        if not missing:
            print(f"Bus{bus} : OK")
        else:
            print(f"Bus{bus} : MISSING {' '.join('0x'+m for m in sorted(missing))}")

    print("")
    print("=================== test: RAVE2 I2C IOB Tests  outcome: FAIL ==================")

    # Exit code is the number of missing instances across both busses (1..6)
    return total_missing_instances if total_missing_instances > 0 else 1


if __name__ == "__main__":
    sys.exit(main())