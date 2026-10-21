# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, IC
# Maintainer: KC

import subprocess
import netifaces
import re
from typing import Tuple, Optional

import openhtf as htf
from openhtf import PhaseResult
from openhtf.output.callbacks import json_factory
from pathlib import Path
import sys
import os
import time


def resolve_interface(iface):
    interfaces = netifaces.interfaces()
    if iface in interfaces:
        return iface
    aliases = {"eth0": "end0", "end0": "eth0"}
    alias = aliases.get(iface)
    if alias in interfaces:
        return alias
    raise RuntimeError(f"Network interface {iface} is not available")


def run_iperf3(cmd, logger, label, timeout=30):
    """Run iperf3 once with timeout."""
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
        )
        if proc.returncode == 0:
            return proc.stdout
        msg = (proc.stderr or proc.stdout or "").strip()
        logger.error(f"iperf3 {label}: exit {proc.returncode}: {msg}")
    except subprocess.TimeoutExpired:
        logger.error(f"iperf3 {label} timed out ({timeout}s)")
        subprocess.run(["killall", "-9", "iperf3"], capture_output=True)
    return None


def wait_for_ip(iface, logger, timeout=30, interval=2):
    """Wait for an interface to acquire an IPv4 address via DHCP."""
    for elapsed in range(0, timeout, interval):
        try:
            addrs = netifaces.ifaddresses(iface)
            if netifaces.AF_INET in addrs:
                ip = addrs[netifaces.AF_INET][0]["addr"]
                logger.info(f"{iface} has IP {ip} (waited {elapsed}s)")
                return ip
        except (ValueError, KeyError):
            pass
        logger.info(f"Waiting for {iface} to get an IP... ({elapsed}s/{timeout}s)")
        time.sleep(interval)
    return None


@htf.measures(
    htf.Measurement("iperf_tx_mbps"),
    htf.Measurement("iperf_tx_retransmits"),
    htf.Measurement("iperf_rx_mbps"),
    htf.Measurement("iperf_rx_retransmits"),
    htf.Measurement("iperf_interface_ip"),
)
def iperf_test_full_duplex(test):
    IPERF_SERVER = os.environ.get("IPERF_SERVER", "192.168.13.5") # Ip Address
    IFACE = resolve_interface(os.environ.get("IPERF_IFACE", "end0"))
    MIN_BW = int(os.environ.get("IPERF_MIN_BW", "800")) #Mbps

    # ------------------------------------------------------------
    # Isolate interface: bring down the OTHER interface to prevent
    # asymmetric routing when both are on the same subnet.
    # ------------------------------------------------------------
    OTHER_IFACE = "eth1" if IFACE in ("end0", "eth0") else resolve_interface("end0")
    test.logger.info(f"Bringing down {OTHER_IFACE} to isolate {IFACE}")
    subprocess.run(["ip", "link", "set", OTHER_IFACE, "down"], capture_output=True)

    try:
        # ------------------------------------------------------------
        # Get interface IP (wait if interface was recently re-enabled)
        # ------------------------------------------------------------
        src_ip = wait_for_ip(IFACE, test.logger)
        if src_ip is None:
            test.logger.error(f"Failed to get IP for {IFACE} within timeout")
            return PhaseResult.FAIL_AND_CONTINUE

        test.measurements.iperf_interface_ip = src_ip
        test.logger.info(f"Running iperf3 on {IFACE} ({src_ip}) minimum bandwidth {MIN_BW} Mbps")

        # ------------------------------------------------------------
        # TX test
        # ------------------------------------------------------------
        tx_cmd = ["iperf3", "-c", IPERF_SERVER, "--bind", src_ip,
                  "--bind-dev", IFACE, "--connect-timeout", "5000"]
        tx_out = run_iperf3(tx_cmd, test.logger, "TX")
        if tx_out is None:
            test.logger.error("iperf3 TX test failed")
            return PhaseResult.FAIL_AND_CONTINUE

        tx_mbps, tx_retrans = parse_iperf3_text(tx_out)

        if tx_mbps is None:
            test.logger.error("Failed to parse TX iperf3 output")
            test.logger.error(tx_out)
            return PhaseResult.FAIL_AND_CONTINUE

        # Allow the iperf3 server to reset between tests
        time.sleep(2)

        # ------------------------------------------------------------
        # RX test
        # ------------------------------------------------------------
        rx_cmd = ["iperf3", "-c", IPERF_SERVER, "--bind", src_ip,
                  "--bind-dev", IFACE, "-R", "--connect-timeout", "5000"]
        rx_out = run_iperf3(rx_cmd, test.logger, "RX")
        if rx_out is None:
            test.logger.error("iperf3 RX test failed")
            return PhaseResult.FAIL_AND_CONTINUE

        rx_mbps, rx_retrans = parse_iperf3_text(rx_out)

        if rx_mbps is None:
            test.logger.error("Failed to parse RX iperf3 output")
            test.logger.error(rx_out)
            return PhaseResult.FAIL_AND_CONTINUE

        # ------------------------------------------------------------
        # Store measurements
        # ------------------------------------------------------------
        test.measurements.iperf_tx_mbps = tx_mbps
        test.measurements.iperf_tx_retransmits = tx_retrans
        test.measurements.iperf_rx_mbps = rx_mbps
        test.measurements.iperf_rx_retransmits = rx_retrans

        test.logger.info(f"TX: {tx_mbps:.1f} Mbps (retrans={tx_retrans})")
        test.logger.info(f"RX: {rx_mbps:.1f} Mbps (retrans={rx_retrans})")

        # ------------------------------------------------------------
        # Pass / Fail criteria
        # ------------------------------------------------------------
        if tx_mbps < MIN_BW or rx_mbps < MIN_BW:
            test.logger.error(
                f"Bandwidth below minimum: TX={tx_mbps}, RX={rx_mbps}, MIN={MIN_BW}"
            )
            return PhaseResult.FAIL_AND_CONTINUE

        return PhaseResult.CONTINUE
    finally:
        # Always restore the other interface
        test.logger.info(f"Bringing {OTHER_IFACE} back up")
        subprocess.run(["ip", "link", "set", OTHER_IFACE, "up"], capture_output=True)


def parse_iperf3_text(output: str) -> Tuple[Optional[float], Optional[int]]:
    """
    Parse iperf3 text output and return (mbps, retransmits).

    Works for:
      - normal (TX) mode
      - reverse (RX) mode
      - sender or receiver summary lines

    Returns:
      (mbps, retransmits)
      If parsing fails, returns (None, None)
    """

    # ---------------------------------------------------------
    # Find the FINAL summary line (sender or receiver)
    # ---------------------------------------------------------
    summary_match = re.search(
        r"^\[\s*\d+\]\s+[\d.]+-[\d.]+\s+sec\s+.*?(sender|receiver)$",
        output,
        re.MULTILINE,
    )

    if not summary_match:
        return None, None

    summary_line = summary_match.group(0)

    # ---------------------------------------------------------
    # Extract Mbps
    # ---------------------------------------------------------
    bw_match = re.search(
        r"([\d.]+)\s+([KMG])bits/sec",
        summary_line,
    )
    if not bw_match:
        return None, None

    bw_val = float(bw_match.group(1))
    unit = bw_match.group(2)
    if unit == "G":
        mbps = bw_val * 1000
    elif unit == "K":
        mbps = bw_val / 1000
    else:
        mbps = bw_val

    # ---------------------------------------------------------
    # Extract retransmits (if present)
    # ---------------------------------------------------------
    # Typical formats:
    #   ... 940 Mbits/sec  0 sender
    #   ... 920 Mbits/sec  3 receiver
    retrans_match = re.search(
        r"[KMG]bits/sec\s+(\d+)\s+(sender|receiver)",
        summary_line,
    )

    retrans = int(retrans_match.group(1)) if retrans_match else 0

    return mbps, retrans


def main():

    test = htf.Test(
        iperf_test_full_duplex,
    )
    test.configure(name="IPERF3 Ethernet Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/iperf_test_%(start_time_millis)s.json",
            indent=2
        ),
    )

    outcome = test.execute()
    sys.exit(0 if outcome else 1)


if __name__ == "__main__":
    main()
