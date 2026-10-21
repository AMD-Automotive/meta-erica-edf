# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: LT
# Maintainer: LT

# ---------------------------------------------------------------------------
# 2026.1 (M17) isp-modules driver SRCREV.
#
# UPDATED 2026-07-20: dropped the previous d18e176 pin and returned to the base
# 2026.1 driver (674609a5 = master / xilinx_v2026.1 HEAD).  The MATCHED May-28
# RPU firmware (isp_fw_05282026, banner "MBOX Version : v0.2") REQUIRES the
# mailbox-FIFO commits that d18e176 was dropping:
#
#   674609a  mbox_fifo: align FIFO read memcpy to 8 bytes   <-- needed by MBOX v0.2
#   77e5338  visp: clear -ESHUTDOWN enq_ack markers on stream_off
#
# With d18e176 + the new firmware, visp.ko cannot complete the INIT_FIRMWARE
# handshake: the R52 reaches "=====TASKS CREATION DONE=====" then waits forever,
# so /proc/vsi/isp_subdev0 never appears and init_isp_4cam.sh aborts.
#
# The earlier "4-camera STREAMON 40s timeout" that motivated the d18e176 pin was
# a symptom of pairing the 8B-aligned driver with the OLD (pre-v0.2) firmware.
# With the matched firmware+driver pair the reference design streams all cameras
# cleanly, so the pin is no longer needed (and is actively harmful now).
#
# The single-buffer patch (0012) and the DT-disabled-node async-notifier fix
# (0011) are still applied on top in kernel-module-isp_%.bbappend.  If either
# fails do_patch on this base, rebase it (only the mbox_fifo/stream_off paths
# moved between d18e176 and 674609a5).
# ---------------------------------------------------------------------------
SRCREV = "674609a506d772683f0b781ca3c7e13c5ebe498f"
