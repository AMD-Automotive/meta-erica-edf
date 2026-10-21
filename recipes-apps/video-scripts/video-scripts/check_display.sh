#/bin/sh
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: KC, LT
# Maintainer: LT


# Start dmesg in background and capture its PID
dmesg -W &
DMESG_PID=$!

# Set up trap to kill dmesg when script exits
trap 'kill $DMESG_PID 2>/dev/null' EXIT

rmmod mmi_dc || rmmod -f mmi_dc
rmmod mmi_dptx || rmmod -f mmi_dptx
rmmod mmi_dcdma || rmmod -f mmi_dcdma
modprobe mmi_dcdma
modprobe mmi_dptx
modprobe mmi_dc

sleep .5


MODETEST_OUT=$(modetest -M mmi-dc 2>/dev/null)

CONN=$(printf '%s\n' "$MODETEST_OUT" | awk '
    /^Connectors:/                            { inconn=1; next }
    /^(CRTCs|Encoders|Planes|Frame buffers):/ { inconn=0 }
    inconn && $3 == "connected"               { print $1; exit }
')

if [ -z "$CONN" ]; then
    echo "check_display: no CONNECTED connector on mmi-dc -- is a monitor plugged in?" >&2
    printf '%s\n' "$MODETEST_OUT" >&2
    exit 1
fi

# Pick a mode.  Prefer 1920x1080 when the monitor advertises it
if printf '%s\n' "$MODETEST_OUT" | grep -qw "1920x1080"; then
    MODE="1920x1080"
else
    MODE=$(printf '%s\n' "$MODETEST_OUT" | awk '
        /^Connectors:/  { inconn=1 }
        inconn && /#0 / { for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+x[0-9]+$/) { print $i; exit } }
    ')
    [ -z "$MODE" ] && MODE="1920x1080"
fi

echo "check_display: driving connector ${CONN} at ${MODE} (@XR24) on mmi-dc"
timeout 3  modetest -M mmi-dc -s ${CONN}:${MODE}@XR24
timeout 15 modetest -M mmi-dc -s ${CONN}:${MODE}@XR24
