#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

import subprocess
import sys
import time
from pathlib import Path

import psutil
import openhtf as htf
from openhtf import PhaseResult
from openhtf.output.callbacks import json_factory

@htf.measures(
    htf.Measurement("mem_total_mb"),
    htf.Measurement("mem_used_mb"),
    htf.Measurement("mem_free_mb"),
    htf.Measurement("mem_usage_percent"),
)
def memory_stress_test(test):
    DURATION = 10          # seconds
    VM_PERCENT = 90        # % of RAM to use
    VM_WORKERS = psutil.cpu_count(logical=True)

    mem = psutil.virtual_memory()
    test.measurements.mem_total_mb = mem.total // (1024 * 1024)

    test.logger.info(
        f"Starting memory stress: {VM_PERCENT}% RAM for {DURATION}s "
        f"({VM_WORKERS} workers)"
    )

    try:
        proc = subprocess.Popen([
            "stress-ng",
            "--vm", str(VM_WORKERS),
            "--vm-bytes", f"{VM_PERCENT}%",
            "--vm-keep",
            "--vm-populate",
            "--timeout", f"{DURATION}s",
            "--metrics-brief",
        ])
    except Exception as e:
        test.logger.error(f"Failed to start memory stress: {e}")
        return PhaseResult.FAIL_AND_CONTINUE

    start = time.time()
    while time.time() - start < DURATION:
        mem = psutil.virtual_memory()
        test.logger.info(
            f"Memory: used={mem.used//1024//1024}MB "
            f"free={mem.available//1024//1024}MB "
            f"usage={mem.percent}%"
        )
        time.sleep(1)

    ret = proc.wait(timeout=5)
    if ret != 0:
        test.logger.error("stress-ng memory test failed")
        return PhaseResult.FAIL_AND_CONTINUE

    mem = psutil.virtual_memory()
    test.measurements.mem_used_mb = mem.used // (1024 * 1024)
    test.measurements.mem_free_mb = mem.available // (1024 * 1024)
    test.measurements.mem_usage_percent = mem.percent

    test.logger.info("Memory stress test completed successfully.")
    return PhaseResult.CONTINUE

def main():

    test = htf.Test(
        memory_stress_test,
    )
    test.configure(name="Memory Stress Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/memory_test_%(start_time_millis)s.json",
            indent=2
        ),
    )
    
    outcome = test.execute()
    sys.exit(0 if outcome else 1)

if __name__ == "__main__":
    main()
