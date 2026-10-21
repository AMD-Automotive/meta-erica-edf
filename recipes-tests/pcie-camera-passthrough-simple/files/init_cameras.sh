#!/bin/bash
# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW
set -e

MODE="${1:-mmap}" # mmap | dmabuf
CONFIG="${2:-0}" # 0=1ch, 1=4ch, 2=5ch

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
tmux kill-session -t ep_proxy 2>/dev/null || true
pkill -x ep_proxy 2>/dev/null || true
sleep 1

# Configure ISPs
if (( NCH > 4 )); then
	configure_isp_rave2 2
else
	configure_isp_rave2
fi
sleep $SLEEP_TIME

# Launch cameras briefly, one-by-one. Priming is best-effort (fix 5: don't let a
# transient gst-launch failure abort the whole bring-up under set -e).
gst-launch-1.0 -v v4l2src device=/dev/video0 io-mode=4 num-buffers=$NUM_INIT_BUFFERS ! "video/x-raw, width=$IMX623_W, height=$IMX623_H, format=NV12, framerate=30/1" ! queue ! fakesink || true
sleep $SLEEP_TIME

gst-launch-1.0 -v v4l2src device=/dev/video2 io-mode=4 num-buffers=$NUM_INIT_BUFFERS ! "video/x-raw, width=$IMX623_W, height=$IMX623_H, format=NV12, framerate=30/1" ! queue ! fakesink || true
sleep $SLEEP_TIME

gst-launch-1.0 -v v4l2src device=/dev/video4 io-mode=4 num-buffers=$NUM_INIT_BUFFERS ! "video/x-raw, width=$IMX623_W, height=$IMX623_H, format=NV12, framerate=30/1" ! queue ! fakesink || true
sleep $SLEEP_TIME

gst-launch-1.0 -v v4l2src device=/dev/video6 io-mode=4 num-buffers=$NUM_INIT_BUFFERS ! "video/x-raw, width=$IMX623_W, height=$IMX623_H, format=NV12, framerate=30/1" ! queue ! fakesink || true
sleep $SLEEP_TIME

if (( NCH > 4 )); then
	gst-launch-1.0 -v v4l2src device=/dev/video10 io-mode=4 num-buffers=$NUM_INIT_BUFFERS ! "video/x-raw, width=3840, height=2160, format=NV12, framerate=30/1" ! queue ! fakesink || true
	sleep $SLEEP_TIME
fi

# Select ep_proxy config: resolution by mode, sensor set by channel count.
# fix 4: for 1ch/4ch omit the imx728 (4K) from the config name; keep it for 5ch.
if (( NCH > 4 )); then
	EP_PROXY_CONFIG=/etc/mdb5_1imx728_4imx623_${RES}.json
else
	EP_PROXY_CONFIG=/etc/mdb5_4imx623_${RES}.json
fi

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
tmux new-session -d -s ep_proxy "ep_proxy -j '$EP_PROXY_CONFIG' 2>&1 | tee '$EP_PROXY_LOG_FILE'" </dev/null >/dev/null 2>&1

exit 0

