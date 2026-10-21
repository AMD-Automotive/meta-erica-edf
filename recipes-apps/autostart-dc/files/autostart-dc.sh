#!/usr/bin/env bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

set -euo pipefail

echo "Starting RAVE2 Digitial Cockit autostart script"

echo "Autostart - detecting board"

MACHINE_FILE="/proc/device-tree/model"
CONFIG_FILE="/etc/autostart-dc.conf"

if [[ -f "$MACHINE_FILE" ]]; then

    mach=$(tr -d '\0' < "$MACHINE_FILE")
    echo "Detected machine is ${mach}"

    if [[ "${mach}" != *[Rr][Aa][Vv][Ee]2* ]]; then
        echo "Machine '${mach}' does not contain RAVE2 - skipping autostart"
        exit 0
    fi

    # Load the user-editable configuration (selects attached camera type)
    CAMERA_TYPE="quanta_imx623"
    if [[ -f "$CONFIG_FILE" ]]; then
        echo "Loading configuration from ${CONFIG_FILE}"
        # shellcheck source=/dev/null
        source "$CONFIG_FILE"
    else
        echo "Configuration file not found: ${CONFIG_FILE} - using defaults"
    fi

    if [[ -z "${CAMERA_TYPE}" ]]; then
        echo "No camera type configured - skipping camera configuration"
    else
        camera_script="/usr/bin/config-camera-${CAMERA_TYPE}.sh"
        if [[ -x "$camera_script" ]]; then
            echo "Configuring cameras: ${CAMERA_TYPE} (${camera_script})"
            "$camera_script"
        else
            echo "Camera setup script not found for type '${CAMERA_TYPE}': ${camera_script}"
        fi
    fi

    echo "Configuring PF rerouting"
    config_pf_rerouting.sh

else
    echo "Device-tree machine not detectable - please file a Jira against this machine image"
fi

