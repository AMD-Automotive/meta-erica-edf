# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

import subprocess
import time
import re
from openhtf import PhaseResult
import openhtf as htf
from openhtf.output.callbacks import json_factory
from pathlib import Path
import sys


def get_connected_dp_status_path():
    connectors = sorted(Path("/sys/class/drm").glob("card*-DP-*/status"))
    for connector in connectors:
        if connector.read_text().strip() == "connected":
            return connector
    return connectors[0] if connectors else None


def get_connected_dp_connector_id():
    output = subprocess.check_output(
        ["modetest", "-M", "mmi-dc", "-c"], text=True
    )
    for line in output.splitlines():
        match = re.match(r"\s*(\d+)\s+\d+\s+connected\s+DP-\d+\b", line)
        if match:
            return match.group(1)
    raise RuntimeError("No connected DisplayPort connector found")

@htf.PhaseOptions()
def dp_link_training_test(test):
    try:
        logs = subprocess.check_output(
            ["journalctl", "-kb"], stderr=subprocess.STDOUT
        ).decode()
    except Exception as e:
        test.logger.error(f"Failed to read kernel logs: {e}")
        return PhaseResult.FAIL_AND_CONTINUE

    drm_logs = "\n".join(
        l for l in logs.splitlines() if "drm" in l.lower()
    )

    if "training failed" in drm_logs.lower():
        test.logger.error("DP link training FAILED.")
        return PhaseResult.FAIL_AND_CONTINUE

    test.logger.info("DP link training PASSED.")
    return PhaseResult.CONTINUE


@htf.PhaseOptions()
def dp_detect_monitor(test):
    connector = get_connected_dp_status_path()
    if connector is None:
        test.logger.error("No DisplayPort connector found.")
        return PhaseResult.FAIL_AND_CONTINUE

    status = connector.read_text().strip()

    if status != "connected":
        test.logger.error("DisplayPort monitor not detected.")
        return PhaseResult.FAIL_AND_CONTINUE

    test.logger.info("DisplayPort monitor detected at %s.", connector)
    return PhaseResult.CONTINUE

@htf.PhaseOptions()
def dp_show_test_pattern(test):
    subprocess.run(["modprobe", "mmi_dcdma"], check=True)
    subprocess.run(["modprobe", "mmi_dptx"], check=True)
    subprocess.run(["modprobe", "mmi_dc"], check=True)

    time.sleep(0.1)

    connector_id = get_connected_dp_connector_id()
    test.logger.info("Displaying test pattern on screen for 5 seconds.")
    subprocess.run(
        [
            "timeout", "5", "modetest", "-M", "mmi-dc", "-s",
            f"{connector_id}:1280x720@XR24",
        ],
        check=False,
    )

@htf.PhaseOptions()
def dp_modeprobe(test):
    subprocess.run(["rmmod", "mmi_dc"], check=True)
    subprocess.run(["rmmod", "mmi_dptx"], check=True)
    subprocess.run(["rmmod", "mmi_dcdma"], check=True)

    time.sleep(0.5)

    subprocess.run(["modprobe", "mmi_dcdma"], check=True)
    subprocess.run(["modprobe", "mmi_dptx"], check=True)
    subprocess.run(["modprobe", "mmi_dc"], check=True)

    time.sleep(0.5)
    return PhaseResult.CONTINUE


def main():

    test = htf.Test(
        dp_show_test_pattern,
        dp_detect_monitor,
        dp_link_training_test,
        dp_modeprobe,        
    )
    test.configure(name="Displayport Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/dp_test_%(start_time_millis)s.json",
            indent=2
        ),
    )

    outcome = test.execute()
    sys.exit(0 if outcome else 1)


if __name__ == "__main__":
    main()
