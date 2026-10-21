// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: BW
// Maintainer: BW

/*
 * ep_proxy.h - Endpoint proxy for MDB5 PCIe video bridge
 *
 * Runs on the Versal APU. Captures frames from Xilinx V4L2 VIPP
 * pipeline and communicates frame metadata to the host via shared
 * BRAM mailbox, then triggers MSI-X interrupts.
 */
#ifndef EP_PROXY_H
#define EP_PROXY_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <linux/videodev2.h>

/* Pull in mailbox definitions */
#include "mdb5_bridge.h"

#define EP_MAX_BUFFERS  32
#define EP_DEFAULT_BUFS 4

/*
 * Detector self-test: when fault injection is active, the stamped
 * watermark frame_seq is rewound by this many frames so the host driver
 * MUST observe a backward jump (inversion) and a watermark-vs-mailbox
 * seq mismatch.  Mailbox metadata stays monotonic -- this mimics the
 * real bug signature (stale pixels, correct-looking metadata).
 *
 * Must be larger than the worst-case delivered-frame gap, otherwise
 * low-latency draining (which decimates the delivered stream) can mask
 * the injected jump: a rewind smaller than the drain gap just looks like
 * a normal forward step.  64 frames clears any realistic gap.
 */
#define EP_FAULT_REWIND 64

/*
 * Per-buffer tracking
 */
struct ep_buffer {
	void            *mmap_addr;     /* mmap'd virtual address */
	size_t           mmap_length;   /* mmap'd length */
	uint64_t         phys_addr;     /* physical DDR address */
	int              dmabuf_fd;     /* exported dmabuf fd, or -1 */
};

/*
 * Per-channel state
 */
struct ep_channel {
	int              index;
	char             dev_path[64];  /* e.g. "/dev/video0" */
	int              v4l2_fd;
	enum v4l2_buf_type buf_type;    /* MPLANE or single-plane (auto-detected) */

	/* Format (from JSON config) */
	uint32_t         width;
	uint32_t         height;
	uint32_t         pixelformat;   /* V4L2 fourcc */
	uint32_t         bytesperline;
	uint32_t         sizeimage;

	/* Buffers */
	struct ep_buffer buffers[EP_MAX_BUFFERS];
	unsigned int     num_buffers;

	/* Frame counter */
	uint32_t         frame_seq;

	/* BRAM mailbox pointer for this channel */
	volatile struct mdb5_mbox_slot *mbox;

	/* Thread management */
	pthread_t        thread;
	bool             running;
};

/*
 * Global proxy state
 */
struct ep_proxy {
	int                     num_channels;
	struct ep_channel       channels[MDB5_MAX_CHANNELS];

	/* Memory-mapped hardware registers */
	volatile uint32_t      *msix_reg;       /* MSI-X trigger at 0xED800948 */
	volatile void          *bram;           /* BRAM base at 0x0800_0000_8000 */
	volatile struct mdb5_mbox_header *bram_header;
	int                     mem_fd;

	/* Config */
	bool                    low_latency;    /* drain stale frames after ACK */
	bool                    stamp_seq;      /* stamp in-band frame watermark */
	unsigned int            num_buffers;    /* V4L2 buffers per channel */
	unsigned int            ack_timeout_ms; /* host ACK poll timeout */
	unsigned int            fault_inject;   /* stamp a stale watermark every
						 * Nth frame (0=off) -- detector
						 * self-test only, NOT for prod */
	char                    config_path[256];
};

/*
 * Convert format string from JSON to V4L2 fourcc.
 * Returns 0 on unrecognized format.
 */
static inline uint32_t ep_format_to_fourcc(const char *fmt)
{
	if (!fmt) return 0;

	if      (!strcasecmp(fmt, "RGB24"))  return V4L2_PIX_FMT_RGB24;
	else if (!strcasecmp(fmt, "BGR24"))  return V4L2_PIX_FMT_BGR24;
	else if (!strcasecmp(fmt, "RGBX32")) return V4L2_PIX_FMT_RGBX32;
	else if (!strcasecmp(fmt, "BGRX32")) return V4L2_PIX_FMT_BGRX32;
	else if (!strcasecmp(fmt, "XRGB32")) return V4L2_PIX_FMT_XRGB32;
	else if (!strcasecmp(fmt, "XBGR32")) return V4L2_PIX_FMT_XBGR32;
	else if (!strcasecmp(fmt, "YUYV"))   return V4L2_PIX_FMT_YUYV;
	else if (!strcasecmp(fmt, "UYVY"))   return V4L2_PIX_FMT_UYVY;
	else if (!strcasecmp(fmt, "YVYU"))   return V4L2_PIX_FMT_YVYU;
	else if (!strcasecmp(fmt, "NV12"))   return V4L2_PIX_FMT_NV12;
	else if (!strcasecmp(fmt, "NV21"))   return V4L2_PIX_FMT_NV21;
	return 0;
}

#endif /* EP_PROXY_H */
