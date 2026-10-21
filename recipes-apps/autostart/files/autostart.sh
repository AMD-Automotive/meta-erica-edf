#!/usr/bin/env bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

set -euo pipefail

echo "Autostart - detecting board"

MACHINE_FILE="/proc/device-tree/machine"

if [[ -f "$MACHINE_FILE" ]]; then

    mach=$(tr -d '\0' < "$MACHINE_FILE")
    echo "Detected machine is ${mach}"

    script="/usr/bin/autostart-${mach}.sh"

    if [[ -x "$script" ]]; then
        echo "Executing autostart script: $script"
        "$script"
    else
        echo "Autostart script not found : $script"
    fi
else
    echo "Device-tree machine not detectable - please file a Jira against this machine image"
fi