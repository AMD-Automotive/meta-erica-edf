#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW
#
# ep_vivid_setup.sh - Create vivid V4L2 sources on the endpoint and
# launch ep_proxy against them.
#
# Intended to be SCPed to the endpoint and executed via SSH by
# pcie_video_tester.py when --endpoint_host is given.
#
# Prints the systemd unit name on the last line of stdout so the
# caller can stop it during teardown.

set -euo pipefail

N_DEVS=${1:-4}
WIDTH=${2:-1920}
HEIGHT=${3:-1080}
FORMAT=${4:-UYVY}
FPS=${5:-30}
EP_PROXY_BIN="/usr/bin/ep_proxy"
CONFIG="/tmp/mdb5_vivid_channels.json"
UNIT="mdb5-vivid-test-$$"

# Map format names to v4l2-ctl pixelformat codes
case "$FORMAT" in
    UYVY)  V4L2FMT="UYVY" ;;
    YUYV)  V4L2FMT="YUYV" ;;
    NV12)  V4L2FMT="NV12" ;;
    RGB24) V4L2FMT="RGB3" ;;
    BGR24) V4L2FMT="BGR3" ;;
    *)     V4L2FMT="$FORMAT" ;;
esac

# vivid default input layout: 0=webcam 1=TV 2=S-Video 3=HDMI
HDMI_INPUT=3

# NV12 is 8bpp on the luma plane: request stride == width so the driver
# does not pad the line
BPL=""
[ "$V4L2FMT" = "NV12" ] && BPL=",bytesperline=$WIDTH"

log() { echo "[ep_vivid_setup] $*" >&2; }

# --- Stop any existing ep_proxy ---
log "stopping any existing ep_proxy"
pkill -x ep_proxy 2>/dev/null || true
sleep 0.5
pkill -9 -x ep_proxy 2>/dev/null || true
sleep 0.5

# --- Unload vivid if loaded ---
for i in $(seq 1 10); do
    lsmod | grep -q '^vivid' || break
    rmmod vivid 2>/dev/null || true
    sleep 0.5
done

# --- Snapshot existing video devices ---
PRE_DEVS=$(ls -1 /dev/video* 2>/dev/null || true)

# --- Load vivid ---
NODES=$(printf '0x1,%.0s' $(seq 1 "$N_DEVS") | sed 's/,$//')
MP=$(printf '0,%.0s' $(seq 1 "$N_DEVS") | sed 's/,$//')
ALLOC=$(printf '1,%.0s' $(seq 1 "$N_DEVS") | sed 's/,$//')

log "loading vivid: n_devs=$N_DEVS"
modprobe vivid "n_devs=$N_DEVS" "node_types=$NODES" \
    "multiplanar=$MP" "allocators=$ALLOC"
sleep 1

# --- Discover new vivid capture devices ---
POST_DEVS=$(ls -1 /dev/video* 2>/dev/null || true)
NEW_DEVS=$(comm -13 <(echo "$PRE_DEVS" | sort) <(echo "$POST_DEVS" | sort))

VIVID_DEVS=()
for dev in $NEW_DEVS; do
    drv=$(v4l2-ctl -d "$dev" -D 2>/dev/null || true)
    if echo "$drv" | grep -qi vivid; then
        caps=$(v4l2-ctl -d "$dev" --all 2>/dev/null || true)
        if echo "$caps" | grep -q "Video Capture"; then
            VIVID_DEVS+=("$dev")
        fi
    fi
done

if [ "${#VIVID_DEVS[@]}" -lt "$N_DEVS" ]; then
    log "WARNING: expected $N_DEVS vivid capture devs, found ${#VIVID_DEVS[@]}"
fi
log "vivid capture devices: ${VIVID_DEVS[*]}"

# --- Configure format, fps, test pattern, and motion ---
# Cycle through visually distinct test patterns per channel:
#   0=75% Colorbar, 4=100% Color Squares, 10=16x16 Checkers, 20=Gray Ramp
PATTERNS=(0 4 10 20)

for i in "${!VIVID_DEVS[@]}"; do
    dev="${VIVID_DEVS[$i]}"
    pat="${PATTERNS[$((i % ${#PATTERNS[@]}))]}"
    # The webcam input only offers discrete frame sizes, so select HDMI
    # to allow arbitrary widths (vivid scales the source to the S_FMT size).
    v4l2-ctl -d "$dev" -i "$HDMI_INPUT"
    v4l2-ctl -d "$dev" --set-fmt-video="width=$WIDTH,height=$HEIGHT,pixelformat=$V4L2FMT$BPL"
    v4l2-ctl -d "$dev" --set-parm "$FPS"
    v4l2-ctl -d "$dev" --set-ctrl "test_pattern=$pat"
    v4l2-ctl -d "$dev" --set-ctrl "horizontal_movement=4"
    v4l2-ctl -d "$dev" --set-ctrl "vertical_movement=4"
    log "  $dev: pattern=$pat (moving)"
done

# --- Generate ep_proxy config ---
CHANNELS=""
for dev in "${VIVID_DEVS[@]}"; do
    [ -n "$CHANNELS" ] && CHANNELS="$CHANNELS,"
    CHANNELS="$CHANNELS{\"v4l2_dev\":\"$dev\",\"width\":$WIDTH,\"height\":$HEIGHT,\"format\":\"$FORMAT\"}"
done
# Remove first: a stale file owned by another user can't be overwritten
# even by root in sticky /tmp (fs.protected_regular)
rm -f "$CONFIG"
echo "{\"low_latency\":true,\"channels\":[$CHANNELS]}" > "$CONFIG"
log "wrote config: $CONFIG"

# --- Launch ep_proxy ---
if [ ! -x "$EP_PROXY_BIN" ]; then
    log "ERROR: $EP_PROXY_BIN not found"
    exit 1
fi

systemd-run --unit="$UNIT" --collect "$EP_PROXY_BIN" -j "$CONFIG"
sleep 2

if systemctl is-active --quiet "$UNIT"; then
    log "ep_proxy active as $UNIT"
else
    log "ERROR: ep_proxy failed to start"
    journalctl -u "$UNIT" --no-pager | tail -20 >&2
    exit 1
fi

# Print the unit name on stdout for the caller to parse
echo "$UNIT"
