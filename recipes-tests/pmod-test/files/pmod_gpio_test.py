#!/usr/bin/env python3
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

"""
pmod_gpio_test.py

PMOD GPIO loopback test for gpiochip0 (0xb0130000).

A test harness connects pairs of pins on the PMOD header:

  PMOD GPIO Test Header (2x6), front view
  +---+---+---+---+---+---+
  | A | B | C | D |GND|VCC|
  | | | | | | | | |   |   |
  | A | B | C | D |GND|VCC|
  +---+---+---+---+---+---+

Each pair is tested by driving one line as output and reading the
other as input, for both HIGH and LOW, then swapping direction.
This verifies every GPIO bit can both drive and receive.

Exit code:
  - 0 = all pass
  - >0 = number of failures
  - 77 = skip (must run as root)
"""

import os
import sys
import time

import gpiod

CHIP = "/dev/gpiochip0"

# PMOD mux (AXI GPIO @ 0xB0130000, == gpiochip0). Line 23 is the PMOD routing
# select (a2b_gpio_sel = ~gpio_o[23]); it must be driven high for the GPIO
# loopback pins to be routed to the header.
#
# NOTE: The mux line and the loopback pins are on the SAME AXI GPIO block. The
# Xilinx gpio driver keeps a software shadow of the data register and rewrites
# the whole word on every line drive, so nudging the bit outside the driver
# (e.g. via /dev/mem) does NOT survive: the first gpiod line drive wipes it back
# to the shadow value. We therefore drive the mux line through gpiod and HOLD
# the request for the whole test so it stays part of the driver shadow.
MUX_LINE = 23

V = gpiod.line.Value


def read_line(chip, line):
    """Read the current logic level of a line via gpiod (0 or 1)."""
    cfg = {(line,): gpiod.LineSettings(direction=gpiod.line.Direction.INPUT)}
    with gpiod.request_lines(chip, consumer="pmod-test", config=cfg) as req:
        return 1 if req.get_values([line])[0] == V.ACTIVE else 0


# Pairs: (label, line_top, line_bottom)
PAIRS = [
    ("A", 13, 19),
    ("B",  3, 16),
    ("C", 15, 20),
    ("D", 12, 18),
]


def test_drive(chip, label, drv_line, rd_line, drv_val, log):
    """Drive drv_line to drv_val, read rd_line. Return True if they match."""
    expected = V.ACTIVE if drv_val else V.INACTIVE
    config = {
        (drv_line,): gpiod.LineSettings(
            direction=gpiod.line.Direction.OUTPUT,
            output_value=expected,
        ),
        (rd_line,): gpiod.LineSettings(
            direction=gpiod.line.Direction.INPUT,
        ),
    }
    with gpiod.request_lines(chip, consumer="pmod-test", config=config) as req:
        time.sleep(0.005)
        actual = req.get_values([rd_line])[0]

    ok = actual == expected
    val_str = "HIGH" if drv_val else "LOW"
    log.write(f"  drive gp{drv_line}->{val_str}  read gp{rd_line}={actual.name}  "
              f"{'OK' if ok else 'FAIL'}\n")
    return ok


def main() -> int:
    if os.geteuid() != 0:
        print("SKIP (must run as root)")
        return 77

    os.makedirs("/tmp/output", exist_ok=True)
    timestamp = int(time.time())
    logfile = f"/tmp/output/pmod_test_{timestamp}.txt"

    with open(logfile, "w") as log:
        log.write("PMOD GPIO Loopback Test\n")
        log.write(f"Start time: {time.ctime()}\n")
        log.write(f"Chip: {CHIP}\n\n")

        total = 0
        pass_count = 0
        fail_count = 0
        pair_results = {}

        num_tests = len(PAIRS) * 4
        print("")
        print("PMOD GPIO Loopback Test")
        print("-----------------------")

        # This test needs the mux line high (GPIO mode) for the whole run. Read
        # its current state via gpiod, then HOLD it high through gpiod (see note
        # above) so the setting survives every subsequent line drive. The held
        # request is released (reverting the line) in the finally block.
        orig_bit = read_line(CHIP, MUX_LINE)
        if orig_bit:
            msg = f"PMOD mux line {MUX_LINE}: keeping mux set for GPIO"
        else:
            msg = f"PMOD mux line {MUX_LINE}: modifying mux from I2S->GPIO"
        print(msg)
        log.write(msg + "\n\n")

        mux_cfg = {
            (MUX_LINE,): gpiod.LineSettings(
                direction=gpiod.line.Direction.OUTPUT,
                output_value=V.ACTIVE,
            ),
        }
        mux_req = gpiod.request_lines(CHIP, consumer="pmod-test-mux",
                                      config=mux_cfg)
        try:
            for label, top, bot in PAIRS:
                pair_ok = True
                log.write(f"Pair {label}: gp{top} <-> gp{bot}\n")

                for drv, rd, direction in [
                    (top, bot, "top->bot"),
                    (bot, top, "bot->top"),
                ]:
                    for val in [1, 0]:
                        total += 1
                        val_str = "HIGH" if val else "LOW"
                        desc = f"Pair {label} {direction} {val_str}"

                        ok = test_drive(CHIP, label, drv, rd, val, log)
                        if ok:
                            log.write(f"ok {total} {desc}\n")
                            pass_count += 1
                        else:
                            log.write(f"not ok {total} {desc}\n")
                            fail_count += 1
                            pair_ok = False

                pair_result = "PASS" if pair_ok else "**FAIL**"
                print(f"Pair {label} - {pair_result}")
                pair_results[label] = pair_ok
                log.write("\n")
        finally:
            # Revert the mux line to its original level, then release it.
            mux_req.set_values(
                {MUX_LINE: (V.ACTIVE if orig_bit else V.INACTIVE)})
            mux_req.release()
            if orig_bit:
                rmsg = f"PMOD mux line {MUX_LINE}: leaving mux set for GPIO"
            else:
                rmsg = f"PMOD mux line {MUX_LINE}: reverting mux from GPIO->I2S"
            print(rmsg)
            log.write(rmsg + "\n")

        # ASCII art summary
        def link(label):
            return "|" if pair_results.get(label, False) else "X"

        art = (
            "\n"
            "  PMOD GPIO Test Header\n"
            "  +---+---+---+---+---+---+\n"
            "  | A | B | C | D |GND|VCC|\n"
            f"  | {link('A')} | {link('B')} | {link('C')} | {link('D')} |   |   |\n"
            "  | A | B | C | D |GND|VCC|\n"
            "  +---+---+---+---+---+---+\n"
        )

        print(art)
        log.write(art)

        log.write(f"Pass={pass_count}\n")
        log.write(f"Fail={fail_count}\n")

    pairs_pass = sum(1 for v in pair_results.values() if v)
    pairs_fail = sum(1 for v in pair_results.values() if not v)

    print(f"Pass : {pairs_pass}")
    print(f"Fail : {pairs_fail}")
    print(f"Log  : {logfile}")
    print("")

    if pairs_fail:
        hint = ("Please ensure the PMOD test harness is correctly installed "
                "in the IO board")
        print(hint)

    outcome = "PASS" if pairs_fail == 0 else "FAIL"
    print(f"=================== test: PMOD GPIO Loopback Test  outcome: {outcome} ==================")

    return pairs_fail


if __name__ == "__main__":
    sys.exit(main())
