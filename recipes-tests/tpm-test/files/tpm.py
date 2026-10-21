#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

import subprocess
import sys
import time
from pathlib import Path

TPM2_ERROR_MAP = {
    0: "SUCCESS",
    1: "GENERAL_ERROR",
    2: "OPTIONS_ERROR",
    3: "AUTHENTICATION_ERROR",
    4: "TCTI_ERROR (TPM transport/device issue)",
    5: "UNSUPPORTED_SCHEME",
}

def classify_error(rc):
    return TPM2_ERROR_MAP.get(rc, "UNKNOWN_ERROR_CODE")


epoch = int(time.time())
log_dir = Path("/tmp/output")
log_dir.mkdir(parents=True, exist_ok=True)
log_file_path = log_dir / f"tpm_test_{epoch}.txt"
log_file = open(log_file_path, "w")


def log(msg, err=False):
    """Print to stdout/stderr AND write to log file."""
    if err:
        print(msg, file=sys.stderr)
    else:
        print(msg)

    log_file.write(msg + "\n")
    log_file.flush()


def run_cmd(cmd):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
        return proc.returncode, proc.stdout.strip(), proc.stderr.strip()
    except FileNotFoundError:
        return 127, "", f"{cmd[0]} not found"


def run_selftest():
    rc, out, err = run_cmd(["tpm2_selftest", "-f"])
    log(f"[SELFTEST] rc={rc} status={classify_error(rc)}")

    if out:
        log(out)
    if err:
        log(err, err=True)

    return rc


def run_gettestresult():
    rc, out, err = run_cmd(["tpm2_gettestresult"])
    log(f"[GETTESTRESULT] rc={rc} status={classify_error(rc)}")

    if out:
        log(out)
    else:
        log("[INFO] No self-test failures reported by TPM.")

    if err:
        log(err, err=True)

    return rc


def parse_getcap_summary(output):
    important_keys = [
        "TPM2_PT_MANUFACTURER",
        "TPM2_PT_VENDOR_STRING_1",
        "TPM2_PT_VENDOR_STRING_2",
        "TPM2_PT_FIRMWARE_VERSION_1",
        "TPM2_PT_FIRMWARE_VERSION_2",
        "TPM2_PT_MODES",
    ]

    lines = output.splitlines()
    summary = []
    capture = False

    for line in lines:
        if any(key in line for key in important_keys):
            capture = True
            summary.append(line)
            continue

        if capture:
            if line.startswith("  "):
                summary.append(line)
            else:
                capture = False

    return "\n".join(summary)


def run_getcap(debug=False):
    rc, out, err = run_cmd(["tpm2_getcap", "properties-fixed"])

    log(f"[GETCAP properties-fixed] rc={rc} status={classify_error(rc)}")

    if out:
        log_file.write("\n--- tpm2_getcap properties-fixed (full output) ---\n")
        log_file.write(out + "\n")
        log_file.write("--- end ---\n")
        log_file.flush()

    if err:
        log_file.write("\n--- stderr ---\n")
        log_file.write(err + "\n")
        log_file.write("--- end stderr ---\n")
        log_file.flush()

    if debug and out:
        log(out)

    return rc


def main():
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        print("SKIP: must be run as root")
        sys.exit(77)

    debug = "--debug" in sys.argv
    overall_rc = 0

    log(f"[INFO] Log file: {log_file_path}")

    if not Path("/dev/tpm0").exists() and not Path("/dev/tpmrm0").exists():
        log("FAIL: No TPM device found (/dev/tpm0 and /dev/tpmrm0 both missing)")
        log("=================== test: Versal2 TPM  outcome: FAIL ==================")
        log_file.close()
        sys.exit(1)

    rc_selftest = run_selftest()
    if rc_selftest != 0:
        log("FAIL: tpm2_selftest failed")
        overall_rc = rc_selftest

    rc_get = run_gettestresult()
    if rc_get != 0:
        log("FAIL: tpm2_gettestresult failed")
        if overall_rc == 0:
            overall_rc = rc_get

    rc_cap = run_getcap(debug=debug)
    if rc_cap != 0:
        log("FAIL: tpm2_getcap properties-fixed failed")
        if overall_rc == 0:
            overall_rc = rc_cap

    if overall_rc == 0:
        log("=================== test: Versal2 TPM  outcome: PASS ==================")
    else:
        log("=================== test: Versal2 TPM  outcome: FAIL ==================")

    log_file.close()
    sys.exit(overall_rc)


if __name__ == "__main__":
    import os
    main()
