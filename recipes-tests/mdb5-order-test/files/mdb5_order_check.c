// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: BW
// Maintainer: BW
/*
 * mdb5_order_check.c - Host-side frame-order integrity checker for the
 * MDB5 PCIe video bridge.
 *
 * Runs on the x86 root complex.  Opens every given /dev/videoN
 * concurrently (the reordering bug only appears under simultaneous
 * multi-channel load), streams via MMAP, and for each dequeued frame
 * checks temporal order using the endpoint's in-band watermark.
 *
 * Two independent order signals are checked per channel:
 *   - pixel watermark: struct mdb5_frame_wm decoded from the frame
 *     payload (offset 0).  This is ground truth -- it reflects what the
 *     pixels actually are, regardless of the driver.
 *   - driver sequence: v4l2_buffer.sequence returned by DQBUF, which the
 *     mdb5_v4l2 driver sets from the watermark when wm_decode=1.  We
 *     cross-check it against the pixel watermark.
 *
 * An inversion (watermark seq <= the previous delivered seq on that
 * channel) is the signature of the frame-buffer-reuse race.  Forward
 * gaps (seq jumped by >1) are dropped frames and are reported but do not
 * count as failures.
 *
 * Emits a human log to stderr and a single JSON summary object to stdout.
 *
 * Usage:
 *   mdb5_order_check [--duration SECS] [--buffers N] /dev/videoA /dev/videoB ...
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include "mdb5_frame_wm.h"

#define MAX_DEVS    16
#define MAX_BUFS    32

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

struct dev_state {
	char            path[128];
	int             fd;
	unsigned int    expected_ch;   /* index in the device list */

	struct { void *addr; size_t len; } bufs[MAX_BUFS];
	unsigned int    nbufs;

	/* stats */
	unsigned long   frames;
	unsigned long   wm_corrupt;    /* bad/missing magic in payload */
	unsigned long   inversions;    /* wm seq <= last delivered */
	unsigned long   gaps;          /* wm seq jumped forward by >1 */
	unsigned long   ch_mismatch;   /* wm.ch_index != expected_ch */
	unsigned long   drv_mismatch;  /* driver vb.sequence != wm.frame_seq */
	unsigned long long max_back_jump;
	uint32_t        last_seq;
	int             have_last;
	uint32_t        first_seq;
	int             have_first;
};

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
	return r;
}

static int dev_setup(struct dev_state *d, unsigned int nbufs)
{
	d->fd = open(d->path, O_RDWR | O_NONBLOCK);
	if (d->fd < 0) {
		fprintf(stderr, "%s: open: %s\n", d->path, strerror(errno));
		return -1;
	}

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(d->fd, VIDIOC_G_FMT, &fmt) < 0) {
		fprintf(stderr, "%s: G_FMT: %s\n", d->path, strerror(errno));
		return -1;
	}

	struct v4l2_requestbuffers req = {0};
	req.count = nbufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(d->fd, VIDIOC_REQBUFS, &req) < 0) {
		fprintf(stderr, "%s: REQBUFS: %s\n", d->path, strerror(errno));
		return -1;
	}
	d->nbufs = req.count > MAX_BUFS ? MAX_BUFS : req.count;

	for (unsigned int i = 0; i < d->nbufs; i++) {
		struct v4l2_buffer b = {0};
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (xioctl(d->fd, VIDIOC_QUERYBUF, &b) < 0) {
			fprintf(stderr, "%s: QUERYBUF %u: %s\n",
				d->path, i, strerror(errno));
			return -1;
		}
		d->bufs[i].len = b.length;
		d->bufs[i].addr = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
				       MAP_SHARED, d->fd, b.m.offset);
		if (d->bufs[i].addr == MAP_FAILED) {
			fprintf(stderr, "%s: mmap %u: %s\n",
				d->path, i, strerror(errno));
			return -1;
		}
		if (xioctl(d->fd, VIDIOC_QBUF, &b) < 0) {
			fprintf(stderr, "%s: QBUF %u: %s\n",
				d->path, i, strerror(errno));
			return -1;
		}
	}

	enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(d->fd, VIDIOC_STREAMON, &t) < 0) {
		fprintf(stderr, "%s: STREAMON: %s\n", d->path, strerror(errno));
		return -1;
	}
	fprintf(stderr, "%s: streaming (%u buffers, %ux%u)\n",
		d->path, d->nbufs, fmt.fmt.pix.width, fmt.fmt.pix.height);
	return 0;
}

static void dev_drain(struct dev_state *d)
{
	for (;;) {
		struct v4l2_buffer b = {0};
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		if (xioctl(d->fd, VIDIOC_DQBUF, &b) < 0)
			break;  /* EAGAIN: nothing more ready */

		d->frames++;

		/* Decode the watermark straight from the payload. */
		uint32_t seq = 0;
		int wm_ok = 0;
		if (d->bufs[b.index].len >= sizeof(struct mdb5_frame_wm)) {
			struct mdb5_frame_wm wm;
			memcpy(&wm, d->bufs[b.index].addr, sizeof(wm));
			if (wm.magic == MDB5_FRAME_WM_MAGIC) {
				wm_ok = 1;
				seq = wm.frame_seq;
				if (wm.ch_index != d->expected_ch)
					d->ch_mismatch++;
				if (b.sequence != wm.frame_seq)
					d->drv_mismatch++;
			}
		}

		if (!wm_ok) {
			d->wm_corrupt++;
			xioctl(d->fd, VIDIOC_QBUF, &b);
			continue;
		}

		if (!d->have_first) {
			d->first_seq = seq;
			d->have_first = 1;
		}

		if (d->have_last) {
			if (seq <= d->last_seq) {
				unsigned long long jump =
					(unsigned long long)d->last_seq - seq;
				d->inversions++;
				if (jump > d->max_back_jump)
					d->max_back_jump = jump;
				fprintf(stderr,
					"%s: INVERSION seq %u after %u (back %llu)\n",
					d->path, seq, d->last_seq, jump);
			} else if (seq > d->last_seq + 1) {
				d->gaps++;
			}
		}
		d->last_seq = seq;
		d->have_last = 1;

		xioctl(d->fd, VIDIOC_QBUF, &b);
	}
}

static void emit_json(struct dev_state *devs, unsigned int n,
		      double elapsed_s)
{
	unsigned long tot_inv = 0, tot_corrupt = 0, tot_chmm = 0, tot_drvmm = 0;

	printf("{\n  \"elapsed_s\": %.3f,\n  \"channels\": [\n", elapsed_s);
	for (unsigned int i = 0; i < n; i++) {
		struct dev_state *d = &devs[i];
		unsigned long span = (d->have_first && d->have_last)
			? (unsigned long)(d->last_seq - d->first_seq) + 1 : 0;

		tot_inv += d->inversions;
		tot_corrupt += d->wm_corrupt;
		tot_chmm += d->ch_mismatch;
		tot_drvmm += d->drv_mismatch;

		printf("    {\"dev\": \"%s\", \"expected_ch\": %u, "
		       "\"frames\": %lu, \"ep_seq_span\": %lu, "
		       "\"inversions\": %lu, \"max_backward_jump\": %llu, "
		       "\"gaps\": %lu, \"wm_corrupt\": %lu, "
		       "\"ch_mismatch\": %lu, \"drv_seq_mismatch\": %lu}%s\n",
		       d->path, d->expected_ch, d->frames, span,
		       d->inversions, d->max_back_jump, d->gaps,
		       d->wm_corrupt, d->ch_mismatch, d->drv_mismatch,
		       (i + 1 < n) ? "," : "");
	}
	printf("  ],\n");
	printf("  \"totals\": {\"inversions\": %lu, \"wm_corrupt\": %lu, "
	       "\"ch_mismatch\": %lu, \"drv_seq_mismatch\": %lu},\n",
	       tot_inv, tot_corrupt, tot_chmm, tot_drvmm);
	printf("  \"pass\": %s\n}\n",
	       (tot_inv == 0 && tot_chmm == 0) ? "true" : "false");
}

int main(int argc, char **argv)
{
	int duration = 60;
	unsigned int nbufs = 4;

	static struct option opts[] = {
		{"duration", required_argument, 0, 'd'},
		{"buffers",  required_argument, 0, 'b'},
		{0, 0, 0, 0},
	};
	int c;
	while ((c = getopt_long(argc, argv, "d:b:", opts, NULL)) != -1) {
		switch (c) {
		case 'd': duration = atoi(optarg); break;
		case 'b': nbufs = (unsigned int)atoi(optarg); break;
		default:
			fprintf(stderr,
				"Usage: %s [--duration S] [--buffers N] /dev/videoX ...\n",
				argv[0]);
			return 2;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "No devices given\n");
		return 2;
	}

	struct sigaction sa = {0};
	sa.sa_handler = on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	unsigned int ndev = 0;
	struct dev_state devs[MAX_DEVS];
	memset(devs, 0, sizeof(devs));
	for (int i = optind; i < argc && ndev < MAX_DEVS; i++) {
		snprintf(devs[ndev].path, sizeof(devs[ndev].path), "%s", argv[i]);
		devs[ndev].expected_ch = ndev;
		devs[ndev].fd = -1;
		ndev++;
	}

	for (unsigned int i = 0; i < ndev; i++) {
		if (dev_setup(&devs[i], nbufs) < 0) {
			fprintf(stderr, "setup failed for %s\n", devs[i].path);
			return 1;
		}
	}

	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	while (g_running) {
		struct pollfd pfds[MAX_DEVS];
		for (unsigned int i = 0; i < ndev; i++) {
			pfds[i].fd = devs[i].fd;
			pfds[i].events = POLLIN;
			pfds[i].revents = 0;
		}

		int pr = poll(pfds, ndev, 200);
		if (pr < 0) {
			if (errno == EINTR) continue;
			perror("poll");
			break;
		}
		for (unsigned int i = 0; i < ndev; i++)
			if (pfds[i].revents & POLLIN)
				dev_drain(&devs[i]);

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double el = (now.tv_sec - t0.tv_sec) +
			    (now.tv_nsec - t0.tv_nsec) / 1e9;
		if (duration > 0 && el >= duration)
			break;
	}

	struct timespec tend;
	clock_gettime(CLOCK_MONOTONIC, &tend);
	double elapsed = (tend.tv_sec - t0.tv_sec) +
			 (tend.tv_nsec - t0.tv_nsec) / 1e9;

	for (unsigned int i = 0; i < ndev; i++) {
		enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(devs[i].fd, VIDIOC_STREAMOFF, &t);
		for (unsigned int j = 0; j < devs[i].nbufs; j++)
			if (devs[i].bufs[j].addr &&
			    devs[i].bufs[j].addr != MAP_FAILED)
				munmap(devs[i].bufs[j].addr, devs[i].bufs[j].len);
		if (devs[i].fd >= 0)
			close(devs[i].fd);
	}

	emit_json(devs, ndev, elapsed);
	return 0;
}
