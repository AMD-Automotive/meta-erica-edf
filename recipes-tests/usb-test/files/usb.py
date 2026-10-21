# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

import subprocess
import os
import time
import openhtf as htf
from openhtf import PhaseResult
from openhtf.output.callbacks import json_factory
from pathlib import Path
import sys

@htf.measures(
    htf.Measurement("usb_device_count"),
    htf.Measurement("usb_devices"),
    htf.Measurement("usb_speeds"),
)
def usb_basic_test(test):
    test.logger.info("Starting USB basic functionality test...")

    devices = list_usb_devices()
    speeds = get_usb_speeds()

    if not devices:
        test.logger.error("No USB devices detected")
        return PhaseResult.FAIL_AND_CONTINUE

    test.measurements.usb_device_count = len(devices)
    test.measurements.usb_devices = devices
    test.measurements.usb_speeds = speeds

    test.logger.info(f"Detected {len(devices)} USB device(s)")
    for dev in devices:
        test.logger.info(dev)


    test.logger.info("USB basic functionality test PASSED")
    return PhaseResult.CONTINUE

def list_usb_devices():
    """
    Returns lsusb output as list of lines
    """
    output = subprocess.check_output(["lsusb"], text=True)
    return output.strip().splitlines()


def get_usb_speeds():
    """
    Returns dict of device path -> speed (Mbps)
    """
    speeds = {}
    for root, dirs, files in os.walk("/sys/bus/usb/devices"):
        if "speed" in files:
            try:
                with open(os.path.join(root, "speed")) as f:
                    speeds[root] = float(f.read().strip())
            except Exception:
                pass
    return speeds


def find_usb_block_device():
    """
    Returns first removable USB block device (e.g. /dev/sda)
    """
    output = subprocess.check_output(
        ["lsblk", "-o", "NAME,TRAN,RM", "-n"],
        text=True
    )

    for line in output.splitlines():
        name, tran, rm = line.split()
        if tran == "usb" and rm == "1":
            return f"/dev/{name}"
    return None


def usb_rw_test(device, mount_point="/mnt/usb_test"):
    """
    Performs basic write/read test on USB mass storage
    """
    os.makedirs(mount_point, exist_ok=True)

    subprocess.run(["mount", device, mount_point], check=True)

    test_file = os.path.join(mount_point, "usb_test.bin")
    data = os.urandom(1024 * 1024)  # 1MB

    with open(test_file, "wb") as f:
        f.write(data)

    with open(test_file, "rb") as f:
        read_back = f.read()

    subprocess.run(["sync"])
    subprocess.run(["umount", mount_point], check=True)

    return data == read_back



def main():

    test = htf.Test(
        usb_basic_test,
    )
    test.configure(name="USB Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/usb_test_%(start_time_millis)s.json",
            indent=2
        ),
    )

    outcome = test.execute()
    sys.exit(0 if outcome else 1)

if __name__ == "__main__":
    main()
