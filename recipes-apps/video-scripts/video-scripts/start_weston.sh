#/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC
# Maintainer: KC

systemctl daemon-reload
systemctl start weston
export XDG_RUNTIME_DIR=/run/
export QT_PLUGIN_PATH=/usr/lib/plugins
export QT_QPA_PLATFORM=wayland
export QT_WAYLAND_SHELL_INTEGRATION=xdg-shell
export XDG_RUNTIME_DIR=/run/
export WAYLAND_DISPLAY=wayland-0
export VK_USE_PLATFORM_WAYLAND_KHR=1
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
