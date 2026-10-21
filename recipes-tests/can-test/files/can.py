# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC, KC, LT
# Maintainer: KC

import subprocess
import time
import re
import sys

import openhtf as htf
from openhtf import PhaseResult
from openhtf.output.callbacks import json_factory
from pathlib import Path

@htf.measures(
    htf.Measurement("RX packets"),
    htf.Measurement("TX packets"),
    htf.Measurement("RX errors"),
    htf.Measurement("TX errors"),
    htf.Measurement("drops"),
)
def can_sw_loopback_test_can0(test):
    iface = "can0"
    bitrate = 500_000
    SENT_PACKETS = 10

    setup_can_interface(iface, bitrate, loopback=True)

    before = parse_ifconfig(iface)

    send_can_random_frames(iface, SENT_PACKETS)

    time.sleep(1)

    after = parse_ifconfig(iface)

    test.measurements["RX packets"] = after["RX packets"] - before["RX packets"]
    test.measurements["TX packets"] = after["TX packets"] - before["TX packets"]
    test.measurements["RX errors"] = after["RX errors"] - before["RX errors"]
    test.measurements["TX errors"] = after["TX errors"] - before["TX errors"]
    test.measurements["drops"] = after["drops"] - before["drops"]

    if test.measurements["RX packets"] < SENT_PACKETS:
        test.logger.error(f"CAN SW loopback FAIL for can0 interface")
        return PhaseResult.FAIL_AND_CONTINUE

    return PhaseResult.CONTINUE

@htf.measures(
    htf.Measurement("RX packets"),
    htf.Measurement("TX packets"),
    htf.Measurement("RX errors"),
    htf.Measurement("TX errors"),
    htf.Measurement("drops"),
)
def can_sw_loopback_test_can1(test):
    iface = "can1"
    bitrate = 500_000
    SENT_PACKETS = 100

    setup_can_interface(iface, bitrate, loopback=True)

    before = parse_ifconfig(iface)

    send_can_random_frames(iface, SENT_PACKETS)

    time.sleep(1)

    after = parse_ifconfig(iface)

    test.measurements["RX packets"] = after["RX packets"] - before["RX packets"]
    test.measurements["TX packets"] = after["TX packets"] - before["TX packets"]
    test.measurements["RX errors"] = after["RX errors"] - before["RX errors"]
    test.measurements["TX errors"] = after["TX errors"] - before["TX errors"]
    test.measurements["drops"] = after["drops"] - before["drops"]

    if test.measurements["RX packets"] < SENT_PACKETS:
        test.logger.error(f"CAN SW loopback FAIL for can0 interface")
        return PhaseResult.FAIL_AND_CONTINUE

    return PhaseResult.CONTINUE

@htf.measures(
    htf.Measurement("RX packets"),
    htf.Measurement("TX packets"),
    htf.Measurement("RX errors"),
    htf.Measurement("TX errors"),
    htf.Measurement("drops"),
)
def can_hw_loopback_test(test):
    tx_iface = "can0"
    rx_iface = "can1"
    bitrate = 500_000
    SENT_PACKETS = 100

    setup_can_interface(tx_iface, bitrate, loopback=False)
    setup_can_interface(rx_iface, bitrate, loopback=False)

    before_rx = parse_ifconfig(rx_iface)
    before_tx = parse_ifconfig(tx_iface)

    send_can_random_frames(tx_iface, SENT_PACKETS)

    time.sleep(2)

    after_rx = parse_ifconfig(rx_iface)
    after_tx = parse_ifconfig(tx_iface)

    rx_packets = after_rx["RX packets"] - before_rx["RX packets"]
    rx_errors = after_rx["RX errors"] - before_rx["RX errors"]
    drops = after_rx["drops"] - before_rx["drops"]
    tx_packets = after_tx["TX packets"] - before_tx["TX packets"]
    tx_errors = after_tx["TX errors"] - before_tx["TX errors"]

    test.measurements["RX packets"] = rx_packets
    test.measurements["TX packets"] = tx_packets
    test.measurements["RX errors"] = rx_errors
    test.measurements["TX errors"] = tx_errors
    test.measurements["drops"] = drops

    if rx_packets < SENT_PACKETS or rx_errors != 0 or tx_errors != 0:
        test.logger.error(f"CAN HW loopback FAIL for can0 tx")
        return PhaseResult.FAIL_AND_CONTINUE

    tx_iface = "can1"
    rx_iface = "can0"
    bitrate = 500_000
    SENT_PACKETS = 100

    setup_can_interface(tx_iface, bitrate, loopback=False)
    setup_can_interface(rx_iface, bitrate, loopback=False)

    before_rx = parse_ifconfig(rx_iface)
    before_tx = parse_ifconfig(tx_iface)

    send_can_random_frames(tx_iface, SENT_PACKETS)

    time.sleep(2)

    after_rx = parse_ifconfig(rx_iface)
    after_tx = parse_ifconfig(tx_iface)

    rx_packets = after_rx["RX packets"] - before_rx["RX packets"]
    rx_errors = after_rx["RX errors"] - before_rx["RX errors"]
    drops = after_rx["drops"] - before_rx["drops"]
    tx_packets = after_tx["TX packets"] - before_tx["TX packets"]
    tx_errors = after_tx["TX errors"] - before_tx["TX errors"]

    test.measurements["RX packets"] = rx_packets
    test.measurements["TX packets"] = tx_packets
    test.measurements["RX errors"] = rx_errors
    test.measurements["TX errors"] = tx_errors
    test.measurements["drops"] = drops

    if rx_packets < SENT_PACKETS or rx_errors != 0 or tx_errors != 0:
        test.logger.error(f"CAN HW loopback FAIL for can1 tx")
        return PhaseResult.FAIL_AND_CONTINUE

    return PhaseResult.CONTINUE

def run(cmd):
    return subprocess.check_output(cmd, shell=True, text=True)


def setup_can_interface(iface, bitrate, loopback=False):
    run(f"ip link set {iface} down")
    lb = "on" if loopback else "off"
    run(f"ip link set {iface} type can bitrate {bitrate} loopback {lb}")
    run(f"ip link set {iface} up")


def send_can_random_frames(iface, count):
    run(f"cangen {iface} -n {count} -g 10")


def send_can_frames(iface, count, can_id, data):
    for _ in range(count):
        run(f"cansend {iface} {can_id}#{data}")


def parse_ifconfig(iface):
    """
    Parses RX/TX packets, errors, drops from ifconfig output
    """
    out = run(f"ifconfig {iface}")

    stats = {}

    rx_match = re.search(
        r"RX packets (\d+).*?RX errors (\d+).*?dropped (\d+)",
        out,
        re.S,
    )
    tx_match = re.search(
        r"TX packets (\d+).*?TX errors (\d+).*?dropped (\d+)",
        out,
        re.S,
    )

    if rx_match:
        stats["RX packets"] = int(rx_match.group(1))
        stats["RX errors"] = int(rx_match.group(2))
        stats["drops"] = int(rx_match.group(3))

    if tx_match:
        stats["TX packets"] = int(tx_match.group(1))
        stats["TX errors"] = int(tx_match.group(2))

    return stats

def main():

    test = htf.Test(
        can_sw_loopback_test_can0,
        can_sw_loopback_test_can1,
        can_hw_loopback_test,
    )
    test.configure(name="Canbus Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/can_test_%(start_time_millis)s.json",
            indent=2
        ),
    )
    
    outcome = test.execute()
    sys.exit(0 if outcome else 1)

if __name__ == "__main__":
    main()
