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
    htf.Measurement("cpu_usage_samples"),
    htf.Measurement("cpu_temp_samples"),
    htf.Measurement("mem_usage_samples"),    
)
def cpu_memory_stress_parallel(test):
    DURATION = 60          # seconds
    INTERVAL = 1
    VM_PERCENT = 80

    cpu_cores = psutil.cpu_count(logical=True)
    vm_workers = max(1, cpu_cores // 2)

    test.logger.info(
        f"Starting parallel CPU+MEM stress for {DURATION}s "
        f"(cpu={cpu_cores}, vm={vm_workers}, mem={VM_PERCENT}%)"
    )

    try:
        proc = subprocess.Popen([
            "stress-ng",
            "--cpu", str(cpu_cores),
            "--vm", str(vm_workers),
            "--vm-bytes", f"{VM_PERCENT}%",
            "--vm-keep",
            "--vm-populate",
            "--timeout", f"{DURATION}s",
            "--metrics-brief",
        ])
    except Exception as e:
        test.logger.error(f"Failed to start stress-ng: {e}")
        return PhaseResult.FAIL_AND_CONTINUE

    cpu_samples = []
    mem_samples = []
    temp_samples = []

    start = time.time()
    while time.time() - start < DURATION:
        # CPU usage
        cpu = psutil.cpu_percent(interval=INTERVAL)
        cpu_samples.append(cpu)

        # Memory usage
        mem = psutil.virtual_memory()
        mem_samples.append(mem.percent)

        # Temperature
        #temp = 0.0
        #try:
            #temps = psutil.sensors_temperatures()
            #if "coretemp" in temps:
            #    temp = sum(t.current for t in temps["coretemp"]) / len(temps["coretemp"])
            #elif "cpu-thermal" in temps:
            #    temp = temps["cpu-thermal"][0].current
        #except Exception:
        #    pass
        temp = read_cpu_temperature()


        temp_samples.append(temp)

        test.logger.info(
            f"CPU={cpu:.1f}% MEM={mem.percent:.1f}% TEMP={temp:.1f}°C"
        )

    ret = proc.wait(timeout=5)
    if ret != 0:
        test.logger.error("stress-ng returned non-zero exit code")
        return PhaseResult.FAIL_AND_CONTINUE

    test.measurements["cpu_usage_samples"] = cpu_samples
    test.measurements["mem_usage_samples"] = mem_samples
    test.measurements["cpu_temp_samples"]  = temp_samples

    test.logger.info("Parallel CPU+Memory stress completed")
    return PhaseResult.CONTINUE


def read_cpu_temperature():

    # 1) Try psutil (generic systems)
    try:
        temps = psutil.sensors_temperatures()
        for entries in temps.values():
            for t in entries:
                if t.current is not None:
                    return float(t.current)
    except Exception:
        pass

    # 2) Try lm-sensors output (your case: versal_thermal-virtual-0)
    try:
        out = subprocess.check_output(["sensors"], text=True)
        for line in out.splitlines():
            if "temp1:" in line:
                # Example: temp1:        +38.6 C
                return float(line.split("+")[1].split()[0])
    except Exception:
        pass

    # 3) Try sysfs thermal zones
    try:
        for zone in range(10):
            path = f"/sys/class/thermal/thermal_zone{zone}/temp"
            try:
                with open(path) as f:
                    val = int(f.read().strip())
                    if val > 0:
                        return val / 1000.0
            except FileNotFoundError:
                continue
    except Exception:
        pass

    return 0.0

@htf.measures(
    htf.Measurement("cpu_usage"),
    htf.Measurement("cpu_temperature"),
)
def cpu_burn_stressng_temp(test):
    DURATION = 10
    INTERVAL = 1
    samples_cpu = []
    samples_temp = []

    num_cores = psutil.cpu_count(logical=True)
    test.logger.info(
        f"Starting stress-ng CPU burn for {DURATION}s on {num_cores} cores..."
    )

    try:
        proc = subprocess.Popen([
            "stress-ng",
            "--cpu", str(num_cores),
            "--timeout", f"{DURATION}s",
            "--metrics-brief"
        ])
    except Exception as e:
        test.logger.error(f"Failed to start stress-ng: {e}")
        return PhaseResult.FAIL_AND_CONTINUE

    start = time.time()

    while time.time() - start < DURATION:
        cpu = psutil.cpu_percent(interval=INTERVAL)
        samples_cpu.append(cpu)

        temp = 0.0
        temps = psutil.sensors_temperatures() or {}
        if "coretemp" in temps:
            core = temps["coretemp"]
            temp = sum(t.current for t in core) / len(core)
        elif "cpu-thermal" in temps:
            temp = temps["cpu-thermal"][0].current

        samples_temp.append(temp)
        test.logger.info(f"CPU {cpu:.1f}%  Temp {temp:.1f}°C")

    proc.wait(timeout=5)

    test.measurements.cpu_usage = samples_cpu
    test.measurements.cpu_temperature = samples_temp
    return PhaseResult.CONTINUE

def main():

    test = htf.Test(
        cpu_memory_stress_parallel,
        cpu_burn_stressng_temp,
    )
    test.configure(name="CPU Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/cpu_test_%(start_time_millis)s.json",
            indent=2
        ),
    )
    
    outcome = test.execute()
    sys.exit(0 if outcome else 1)

if __name__ == "__main__":
    main()
