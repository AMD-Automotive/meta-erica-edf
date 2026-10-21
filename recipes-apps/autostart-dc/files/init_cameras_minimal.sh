#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: TO
# Maintainer: TO

# init_cameras_minimal.sh - Minimal camera initialization helper.
set -e

echo "init_cameras_minimal.sh - initializing cameras"

# Defaults
MODE="dmabuf"   # mmap | dmabuf
CONFIG="1"    # 0=1ch, 1=4ch, 2=5ch
ISP="1"       # 1=config ISP, 0=don't config
TYPE="leopard"  # quanta | leopard

usage() {
        cat >&2 <<EOF
Usage: init_cameras_minimal.sh [-m mode] [-c config] [-i isp] [-t type] [-h]

  -m mode     Capture mode: mmap | dmabuf           (default: ${MODE})
  -c config   Channel config: 0=1ch, 1=4ch, 2=5ch   (default: ${CONFIG})
  -i isp      Configure ISP: 1=yes, 0=no            (default: ${ISP})
  -t type     Camera type: quanta | leopard         (default: ${TYPE})
  -h          Show this help and exit
EOF
}

while getopts ":m:c:i:t:h" opt; do
        case "$opt" in
                m) MODE="$OPTARG" ;;
                c) CONFIG="$OPTARG" ;;
                i) ISP="$OPTARG" ;;
                t) TYPE="$OPTARG" ;;
                h) usage; exit 0 ;;
                :) echo "ERROR: option -$OPTARG requires an argument" >&2; usage; exit 1 ;;
                \?) echo "ERROR: unknown option -$OPTARG" >&2; usage; exit 1 ;;
        esac
done
shift $((OPTIND - 1))

case "$ISP" in
        0|1) ;;
        *) echo "ERROR: unknown isp '$ISP' (expected 0 or 1)" >&2; usage; exit 1 ;;
esac

case "$TYPE" in
        quanta|leopard) ;;
        *) echo "ERROR: unknown type '$TYPE' (expected quanta or leopard)" >&2; usage; exit 1 ;;
esac

NUM_INIT_BUFFERS=50
SLEEP_TIME=0.5

case "$CONFIG" in
        0) NCH=1 ;;
        1) NCH=4 ;;
        2) NCH=5 ;;
        *) echo "ERROR: unknown config '$CONFIG' (expected 0, 1, or 2)" >&2; exit 1 ;;
esac

# Resolution of the imx623 cameras depends on mode (fix 7): dmabuf uses 1792.
case "$MODE" in
        mmap)   IMX623_W=1920; RES=1920 ;;
        dmabuf) IMX623_W=1792; RES=1792 ;;
        *) echo "ERROR: unknown mode '$MODE' (expected mmap or dmabuf)" >&2; exit 1 ;;
esac
IMX623_H=1536

# Stop any previous ep_proxy so the v4l2 devices are free (fix 3: idempotent
# re-runs; otherwise the priming pipelines below fail "device busy"). ep_proxy
# runs in a detached tmux session (see below), so kill that; also pkill any
# stray instance left over from an older launch.
#tmux kill-session -t ep_proxy 2>/dev/null || true
killall  ep_proxy 2>/dev/null || true
pkill -x ep_proxy 2>/dev/null || true
sleep 1

echo

if (( ISP == 1 )); then
   echo "Running configure_isp_rave2 (type: $TYPE)"
   # Configure ISPs based on camera type
   case "$TYPE" in
        leopard) configure_isp_rave2 ;;
        quanta)  configure_isp_rave2 0 quanta ;;
   esac
   sleep $SLEEP_TIME
else
   echo "Skipping configure_isp_rave2"
fi


# Select ep_proxy config: resolution by mode.
EP_PROXY_CONFIG=/etc/mdb5_4imx623_${RES}.json

EP_PROXY_LOG_FILE="/tmp/ep_proxy_$(date +%Y%m%d_%H%M%S).log"
echo "Launching ep_proxy -j $EP_PROXY_CONFIG in tmux session 'ep_proxy' (log: $EP_PROXY_LOG_FILE)"
# fix 2: run ep_proxy in a detached tmux session so it is fully divorced from
# the SSH session. Plain setsid+backgrounding was not enough — ep_proxy keeps an
# inherited fd on the SSH channel. tmux alone is not enough either: if no tmux
# server is running yet, `tmux new-session` spawns the server as a child of this
# SSH session, and that server inherits the SSH channel fds and holds them open
# for as long as ep_proxy (its only session) runs. Redirecting the tmux
# invocation's stdio to /dev/null makes the spawned server inherit /dev/null
# instead, so the SSH command returns immediately while ep_proxy keeps running.
# Attach with `tmux attach -t ep_proxy` to watch it live.
/usr/bin/tmux new-session -d -s ep_proxy "ep_proxy -j '$EP_PROXY_CONFIG' 2>&1 | tee '$EP_PROXY_LOG_FILE'" </dev/null >/dev/null 2>&1


echo "init_cameras_minimal.sh - complete"
exit 0
