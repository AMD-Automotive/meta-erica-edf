# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC
# Maintainer: KC

import subprocess
import time
import psutil
import openhtf as htf
from openhtf.output.callbacks import json_factory
from pathlib import Path
import sys


def get_ethernet_interface():
    interfaces = psutil.net_if_stats()
    for iface in ("end0", "eth0"):
        if iface in interfaces:
            return iface
    raise RuntimeError("Neither end0 nor eth0 is available")


@htf.measures(
    htf.Measurement("link_status"),
    htf.Measurement("advertised_speed"),
    htf.Measurement("link_duplex"),
    htf.Measurement("mac_address"),
)
def ethernet_test(test):
    iface = get_ethernet_interface()
    test.logger.info("Testing Ethernet interface %s", iface)

    for i in range(10):
        subprocess.run(["ip", "link", "set", iface, "up"], check=True)
        subprocess.run(["ip", "link", "set", iface, "down"], check=True)
        subprocess.run(["ip", "link", "set", iface, "up"], check=True)
    time.sleep(5)

    stats = psutil.net_if_stats()[iface]
    addrs = psutil.net_if_addrs()[iface]

    test.measurements.link_status = "up" if stats.isup else "down"
    test.measurements.advertised_speed = stats.speed
    test.measurements.link_duplex = (
        "full" if stats.duplex == psutil.NIC_DUPLEX_FULL else "half"
    )

    mac = next(a.address for a in addrs if a.family == psutil.AF_LINK)
    test.measurements.mac_address = mac


def main():
 
    test = htf.Test(
        ethernet_test
    )
    test.configure(name="Ethernet Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(

        json_factory.OutputToJSON(
            "/tmp/output/ethernet_test_%(start_time_millis)s.json",
            indent=2
        ),
    )

    outcome = test.execute()
    sys.exit(0 if outcome else 1)


if __name__ == "__main__":
    main()

