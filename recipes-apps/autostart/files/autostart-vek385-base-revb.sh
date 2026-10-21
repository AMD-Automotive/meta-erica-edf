# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO, KC
# Maintainer: TO

echo "vek385-base-revb.sh Autostart script"

echo "Reading the camera mode DIP switch"
vek385_read_mode_gpio.sh

echo "Configuring cameras"
vek385_configure_cameras
