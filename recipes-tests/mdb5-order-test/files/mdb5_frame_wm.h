/* Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved. */
/* SPDX-License-Identifier: MIT */
/* Contributors: BW */
/* Maintainer: BW */
/*
 * mdb5_frame_wm.h - Standalone definition of the MDB5 in-band frame
 * watermark, for the host-side order checker.
 *
 * This MUST stay in sync with struct mdb5_frame_wm in mdb5_bridge.h
 * (ep_proxy / mdb5_v4l2 driver).  It is duplicated here so the checker
 * builds with nothing but a libc toolchain on the host.
 */
#ifndef MDB5_FRAME_WM_H
#define MDB5_FRAME_WM_H

#include <stdint.h>

#define MDB5_FRAME_WM_MAGIC 0x4D445746U  /* ASCII "MDWF" */

struct mdb5_frame_wm {
	uint32_t magic;            /* MDB5_FRAME_WM_MAGIC when valid */
	uint32_t ch_index;         /* endpoint channel index */
	uint32_t frame_seq;        /* endpoint monotonic frame sequence */
	uint32_t reserved;
	uint64_t capture_ktime_ns; /* endpoint CLOCK_MONOTONIC at DQBUF */
} __attribute__((packed));         /* 24 bytes */

#endif /* MDB5_FRAME_WM_H */
