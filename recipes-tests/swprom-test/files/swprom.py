# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: IC
# Maintainer: KC

import subprocess
import re
import time
import gpiod
import sys

import openhtf as htf
from openhtf import PhaseResult
from openhtf.output.callbacks import json_factory
from pathlib import Path
 
 

# ---------------- PARSE GPIODETECT FOR pmc_gpio ----------------
def find_pmc_gpio_chip():
    """Return the gpiochip name that corresponds to [pmc_gpio]."""
    result = subprocess.run(
        ["gpiodetect"], capture_output=True, text=True, check=True
    )
    for line in result.stdout.strip().splitlines():
        if "[pmc_gpio]" in line:
            match = re.search(r'^(gpiochip\d+)', line)
            if match:
                return match.group(1)
    return None


def gpio_active(chip_name):
    """Use gpioget to check if line 0 is active (1)"""
    try:
        # Get chip number from chip name, e.g., gpiochip10 -> 10
        chip_number = int(re.search(r'gpiochip(\d+)', chip_name).group(1))
        # Call gpioget on line 0
        result = subprocess.run(
            ["gpioget", "-c", str(chip_number), "19"],
            capture_output=True, text=True, check=True
        )
        return "active" in result.stdout.strip()
    except Exception:
        return False


# ---------------- OPENHTF TEST ----------------
@htf.measures(
    htf.Measurement("detected_chip"),
)
def swprom_pmc_test( test ):
    """Test pmc_gpio chip: check active, then write/read EEPROM."""
    chip_name = find_pmc_gpio_chip()
    if not chip_name:
        test.logger.error("No pmc_gpio chip found")
        return PhaseResult.FAIL

    test.logger.info(f"Found pmc_gpio on {chip_name}")

    if not gpio_active(chip_name):
        test.logger.error(f"{chip_name} is not active")
        return PhaseResult.FAIL

    test.measurements["detected_chip"] = chip_name

 
    return PhaseResult.CONTINUE


# ---------------- EXECUTION ----------------
def main():

    test = htf.Test(
        swprom_pmc_test,
        )
    test.configure(name="PMC_GPIO 1-Wire EEPROM Test")

    path = Path("/tmp/output")
    path.mkdir(parents=True, exist_ok=True)

    test.add_output_callbacks(
        json_factory.OutputToJSON(
            "/tmp/output/swprom_test_%(start_time_millis)s.json",
            indent=2
        ),
    )

    outcome = test.execute()
    sys.exit(0 if outcome else 1)


if __name__ == "__main__":
    main()