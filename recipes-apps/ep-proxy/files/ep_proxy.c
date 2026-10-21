// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: BW, UH
// Maintainer: UH

/*
 * ep_proxy.c - Endpoint proxy for MDB5 PCIe video bridge
 *
 * Runs on the Versal APU. For each video channel:
 *   1. Captures frames from V4L2 capture device
 *   2. Writes frame metadata (DDR address, size) to shared BRAM mailbox
 *   3. Triggers MSI-X interrupt to notify the host
 *   4. Waits for host DMA completion ACK before requeuing the buffer
 *
 * Channel configuration is read from a JSON file.
 *
 * Usage: ep_proxy [-j config.json]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stddef.h>
#include <linux/videodev2.h>

#include "ep_proxy.h"

#define DEFAULT_CONFIG_PATH "/etc/mdb5_channels.json"
#define HOST_ACK_TIMEOUT_MS 100
#define PAGE_SIZE           4096

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* ----------------------------------------------------------------
 * Minimal JSON parser (no external dependency)
 * Parses the channel config JSON into ep_proxy structure.
 * Only handles the specific schema we expect.
 * ---------------------------------------------------------------- */

/* Skip whitespace */
static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/* Parse a JSON string value (between quotes). Returns malloc'd string. */
static char *parse_json_string(const char **pp)
{
	const char *p = *pp;
	p = skip_ws(p);
	if (*p != '"') return NULL;
	p++;
	const char *start = p;
	while (*p && *p != '"') p++;
	if (*p != '"') return NULL;
	size_t len = p - start;
	char *s = malloc(len + 1);
	memcpy(s, start, len);
	s[len] = '\0';
	*pp = p + 1;
	return s;
}

/* Parse a JSON integer value */
static int parse_json_int(const char **pp)
{
	const char *p = skip_ws(*pp);
	int val = (int)strtol(p, (char **)&p, 10);
	*pp = p;
	return val;
}

/* Parse a JSON boolean value */
static bool parse_json_bool(const char **pp)
{
	const char *p = skip_ws(*pp);
	if (strncmp(p, "true", 4) == 0) {
		*pp = p + 4;
		return true;
	}
	*pp = p + (strncmp(p, "false", 5) == 0 ? 5 : 0);
	return false;
}


static int parse_config(struct ep_proxy *proxy, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "Failed to open config: %s: %s\n", path, strerror(errno));
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *json = malloc(fsize + 1);
	if (fread(json, 1, fsize, f) != (size_t)fsize) {
		fprintf(stderr, "Failed to read config file\n");
		free(json);
		fclose(f);
		return -1;
	}
	json[fsize] = '\0';
	fclose(f);

	proxy->num_channels = 0;
	const char *p = json;

	/* Find "channels" array */
	const char *channels_key = strstr(p, "\"channels\"");
	if (!channels_key) {
		fprintf(stderr, "No 'channels' key in config\n");
		free(json);
		return -1;
	}
	p = channels_key + strlen("\"channels\"");
	p = skip_ws(p);
	if (*p == ':') p++;
	p = skip_ws(p);
	if (*p != '[') {
		fprintf(stderr, "Expected '[' after channels key\n");
		free(json);
		return -1;
	}
	p++;

	/* Parse each channel object */
	while (proxy->num_channels < MDB5_MAX_CHANNELS) {
		p = skip_ws(p);
		if (*p == ']') break;
		if (*p == ',') { p++; continue; }
		if (*p != '{') break;
		p++;

		struct ep_channel *ch = &proxy->channels[proxy->num_channels];
		memset(ch, 0, sizeof(*ch));
		ch->index = proxy->num_channels;
		ch->v4l2_fd = -1;

		/* Parse object fields */
		while (*p && *p != '}') {
			p = skip_ws(p);
			if (*p == ',') { p++; continue; }

			char *key = parse_json_string(&p);
			if (!key) break;
			p = skip_ws(p);
			if (*p == ':') p++;

			if (strcmp(key, "v4l2_dev") == 0) {
				char *val = parse_json_string(&p);
				if (val) {
					snprintf(ch->dev_path, sizeof(ch->dev_path), "%s", val);
					free(val);
				}
			} else if (strcmp(key, "width") == 0) {
				ch->width = parse_json_int(&p);
			} else if (strcmp(key, "height") == 0) {
				ch->height = parse_json_int(&p);
			} else if (strcmp(key, "format") == 0) {
				char *val = parse_json_string(&p);
				if (val) {
					ch->pixelformat = ep_format_to_fourcc(val);
					if (!ch->pixelformat) {
						fprintf(stderr, "Unknown format '%s' for channel %d\n",
							val, ch->index);
						free(val);
						free(key);
						free(json);
						return -1;
					}
					free(val);
				}
			} else {
				/* Skip unknown value */
				p = skip_ws(p);
				if (*p == '"') {
					char *val = parse_json_string(&p);
					free(val);
				} else {
					parse_json_int(&p);
				}
			}
			free(key);
		}
		if (*p == '}') p++;

		/* Compute derived values */
		ch->bytesperline = ch->width * mdb5_fmt_bpp(ch->pixelformat);
		ch->sizeimage = mdb5_image_size(ch->pixelformat, ch->width, ch->height);

		printf("Channel %d: %s %ux%u fmt=0x%08x stride=%u size=%u\n",
		       ch->index, ch->dev_path, ch->width, ch->height,
		       ch->pixelformat, ch->bytesperline, ch->sizeimage);

		proxy->num_channels++;
	}

	/* Check for low_latency option */
	const char *ll_key = strstr(json, "\"low_latency\"");
	if (ll_key) {
		const char *q = ll_key + strlen("\"low_latency\"");
		q = skip_ws(q);
		if (*q == ':') q++;
		proxy->low_latency = parse_json_bool(&q);
	}

	/* Optional: in-band watermark stamping (default on) */
	const char *ss_key = strstr(json, "\"stamp_seq\"");
	if (ss_key) {
		const char *q = ss_key + strlen("\"stamp_seq\"");
		q = skip_ws(q);
		if (*q == ':') q++;
		proxy->stamp_seq = parse_json_bool(&q);
	}

	/* Optional: V4L2 buffers per channel */
	const char *nb_key = strstr(json, "\"num_buffers\"");
	if (nb_key) {
		const char *q = nb_key + strlen("\"num_buffers\"");
		q = skip_ws(q);
		if (*q == ':') q++;
		proxy->num_buffers = (unsigned int)parse_json_int(&q);
	}

	/* Optional: host ACK poll timeout (ms) */
	const char *at_key = strstr(json, "\"ack_timeout_ms\"");
	if (at_key) {
		const char *q = at_key + strlen("\"ack_timeout_ms\"");
		q = skip_ws(q);
		if (*q == ':') q++;
		proxy->ack_timeout_ms = (unsigned int)parse_json_int(&q);
	}

	/* Optional: detector self-test fault injection (every Nth frame) */
	const char *fi_key = strstr(json, "\"fault_inject\"");
	if (fi_key) {
		const char *q = fi_key + strlen("\"fault_inject\"");
		q = skip_ws(q);
		if (*q == ':') q++;
		proxy->fault_inject = (unsigned int)parse_json_int(&q);
	}

	free(json);
	return 0;
}

/* ----------------------------------------------------------------
 * Physical address discovery via CMA region scan
 *
 * On Versal, vb2-dma-contig CMA buffers aren't visible in
 * /proc/self/pagemap (uncached remap_pfn_range mapping).
 * Instead we write a unique magic pattern to each buffer via
 * its V4L2 mmap, then scan the known CMA region via /dev/mem
 * to find the matching physical address.
 * ---------------------------------------------------------------- */

/* Versal CMA region from device tree reserved-memory node.
 * The reserved CMA window is 0x800000000-0x93fffffff (5GB); buffers can be
 * allocated anywhere in it, so the magic-pattern scan must cover the whole
 * region, not just the first 512MB. */
#define CMA_BASE        0x800000000ULL
#define CMA_SCAN_SIZE   (5ULL * 1024 * 1024 * 1024)  /* full 5GB CMA region */
#define CMA_SCAN_CHUNK  (2ULL * 1024 * 1024)    /* mmap 2MB at a time */

/* 16-byte magic written at the start of a buffer for phys addr discovery */
struct phys_magic {
	uint64_t sig;       /* 0xDEADBEEF_EP_PROXY */
	uint32_t buf_index; /* which buffer */
	uint32_t checksum;  /* simple validation */
};

#define PHYS_MAGIC_SIG  0xDEADBEEFE9000000ULL

static int64_t discover_phys_addr(int mem_fd, void *buf_va, unsigned int buf_idx, size_t buf_len)
{
	/* Write unique magic pattern */
	struct phys_magic magic = {
		.sig = PHYS_MAGIC_SIG | buf_idx,
		.buf_index = buf_idx,
		.checksum = buf_idx ^ 0xCAFE,
	};
	memcpy(buf_va, &magic, sizeof(magic));

	/* Ensure write reaches DDR */
	__sync_synchronize();

	/* Scan CMA region via /dev/mem */
	for (uint64_t off = 0; off < CMA_SCAN_SIZE; off += CMA_SCAN_CHUNK) {
		void *m = mmap(NULL, CMA_SCAN_CHUNK, PROT_READ, MAP_SHARED,
			       mem_fd, CMA_BASE + off);
		if (m == MAP_FAILED)
			continue;

		for (uint64_t p = 0; p < CMA_SCAN_CHUNK; p += PAGE_SIZE) {
			struct phys_magic *candidate = (struct phys_magic *)
				((char *)m + p);
			if (candidate->sig == magic.sig &&
			    candidate->buf_index == buf_idx &&
			    candidate->checksum == magic.checksum) {
				uint64_t phys = CMA_BASE + off + p;
				munmap(m, CMA_SCAN_CHUNK);
				/* Clear the magic pattern */
				memset(buf_va, 0, sizeof(magic));
				return (int64_t)phys;
			}
		}
		munmap(m, CMA_SCAN_CHUNK);
	}

	return -1;
}

/* ----------------------------------------------------------------
 * V4L2 channel setup
 * ---------------------------------------------------------------- */

static int xioctl(int fd, unsigned long request, void *arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

static int channel_setup_v4l2(struct ep_channel *ch, int mem_fd,
			      unsigned int num_buffers)
{
	ch->v4l2_fd = open(ch->dev_path, O_RDWR);
	if (ch->v4l2_fd < 0) {
		fprintf(stderr, "Ch%d: Failed to open %s: %s\n",
			ch->index, ch->dev_path, strerror(errno));
		return -1;
	}

	/* Detect multiplanar vs single-plane capability */
	struct v4l2_capability cap = {0};
	if (xioctl(ch->v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
		fprintf(stderr, "Ch%d: VIDIOC_QUERYCAP failed: %s\n",
			ch->index, strerror(errno));
		return -1;
	}
	uint32_t devcaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
			   ? cap.device_caps : cap.capabilities;
	if (devcaps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
		ch->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	else if (devcaps & V4L2_CAP_VIDEO_CAPTURE)
		ch->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else {
		fprintf(stderr, "Ch%d: device supports neither capture nor mplane\n",
			ch->index);
		return -1;
	}
	printf("Ch%d: using %s V4L2 API\n", ch->index,
	       ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
	       ? "multiplanar" : "single-plane");

	/* Set format */
	struct v4l2_format fmt = {0};
	fmt.type = ch->buf_type;
	if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt.fmt.pix_mp.width = ch->width;
		fmt.fmt.pix_mp.height = ch->height;
		fmt.fmt.pix_mp.pixelformat = ch->pixelformat;
		fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
		fmt.fmt.pix_mp.num_planes = 1;
	} else {
		fmt.fmt.pix.width = ch->width;
		fmt.fmt.pix.height = ch->height;
		fmt.fmt.pix.pixelformat = ch->pixelformat;
		fmt.fmt.pix.field = V4L2_FIELD_NONE;
	}

	if (xioctl(ch->v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
		fprintf(stderr, "Ch%d: VIDIOC_S_FMT failed: %s\n",
			ch->index, strerror(errno));
		return -1;
	}

	/*
	 * S_FMT bytesperline can be wrong on Xilinx VIPP (reports 3 bpp
	 * for UYVY).  We'll correct from QUERYBUF buffer length below.
	 */
	if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ch->bytesperline = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		ch->sizeimage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		ch->bytesperline = fmt.fmt.pix.bytesperline;
		ch->sizeimage = fmt.fmt.pix.sizeimage;
	}

	/* Request buffers */
	struct v4l2_requestbuffers req = {0};
	req.count = num_buffers;
	req.type = ch->buf_type;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(ch->v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
		fprintf(stderr, "Ch%d: VIDIOC_REQBUFS failed: %s\n",
			ch->index, strerror(errno));
		return -1;
	}
	ch->num_buffers = req.count;
	if (ch->num_buffers > EP_MAX_BUFFERS)
		ch->num_buffers = EP_MAX_BUFFERS;

	printf("Ch%d: allocated %u buffers\n", ch->index, ch->num_buffers);

	/* Map buffers and discover physical addresses */
	for (unsigned int i = 0; i < ch->num_buffers; i++) {
		struct ep_buffer *buf = &ch->buffers[i];

		struct v4l2_plane planes[1] = {0};
		struct v4l2_buffer vbuf = {0};
		vbuf.type = ch->buf_type;
		vbuf.memory = V4L2_MEMORY_MMAP;
		vbuf.index = i;
		if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			vbuf.length = 1;
			vbuf.m.planes = planes;
		}

		if (xioctl(ch->v4l2_fd, VIDIOC_QUERYBUF, &vbuf) < 0) {
			fprintf(stderr, "Ch%d: VIDIOC_QUERYBUF %u failed: %s\n",
				ch->index, i, strerror(errno));
			return -1;
		}

		size_t mmap_len;
		off_t  mmap_off;
		if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			mmap_len = planes[0].length;
			mmap_off = planes[0].m.mem_offset;
		} else {
			mmap_len = vbuf.length;
			mmap_off = vbuf.m.offset;
		}

		buf->mmap_length = mmap_len;
		buf->mmap_addr = mmap(NULL, mmap_len,
				      PROT_READ | PROT_WRITE, MAP_SHARED,
				      ch->v4l2_fd, mmap_off);
		if (buf->mmap_addr == MAP_FAILED) {
			fprintf(stderr, "Ch%d: mmap buf %u failed: %s\n",
				ch->index, i, strerror(errno));
			return -1;
		}

		/* Export as dmabuf to get physical address */
		struct v4l2_exportbuffer expbuf = {0};
		expbuf.type = ch->buf_type;
		expbuf.index = i;
		expbuf.plane = 0;
		buf->dmabuf_fd = -1;

		if (xioctl(ch->v4l2_fd, VIDIOC_EXPBUF, &expbuf) == 0) {
			buf->dmabuf_fd = expbuf.fd;
			printf("Ch%d: buf[%u] dmabuf_fd=%d\n",
			       ch->index, i, buf->dmabuf_fd);
		} else {
			printf("Ch%d: buf[%u] EXPBUF failed: %s\n",
			       ch->index, i, strerror(errno));
		}

		/* Discover physical address via CMA region scan */
		int64_t pa = discover_phys_addr(mem_fd, buf->mmap_addr,
						i, buf->mmap_length);
		if (pa < 0) {
			fprintf(stderr, "Ch%d: Failed to get phys addr for buf %u\n",
				ch->index, i);
			return -1;
		}
		buf->phys_addr = (uint64_t)pa;

		printf("Ch%d: buf[%u] va=%p pa=0x%lx len=%zu\n",
		       ch->index, i, buf->mmap_addr,
		       (unsigned long)buf->phys_addr, buf->mmap_length);
	}

	/*
	 * Override sizeimage from actual buffer allocation.
	 * The VIPP S_FMT can report a bogus bytesperline (e.g. 3 bpp
	 * for UYVY) but the DMA engine allocates correctly.
	 *
	 * bytesperline must remain the Y-plane stride (= width * bpp)
	 * for semi-planar formats like NV12, not sizeimage/height.
	 */
	ch->sizeimage = ch->buffers[0].mmap_length;
	printf("Ch%d: corrected size=%u (from buffer length), stride=%u\n",
	       ch->index, ch->sizeimage, ch->bytesperline);

	/* Queue all buffers */
	for (unsigned int i = 0; i < ch->num_buffers; i++) {
		struct v4l2_plane planes[1] = {0};
		struct v4l2_buffer vbuf = {0};
		vbuf.type = ch->buf_type;
		vbuf.memory = V4L2_MEMORY_MMAP;
		vbuf.index = i;
		if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			vbuf.length = 1;
			vbuf.m.planes = planes;
		}

		if (xioctl(ch->v4l2_fd, VIDIOC_QBUF, &vbuf) < 0) {
			fprintf(stderr, "Ch%d: VIDIOC_QBUF %u failed: %s\n",
				ch->index, i, strerror(errno));
			return -1;
		}
	}

	/* Start streaming */
	enum v4l2_buf_type type = ch->buf_type;
	if (xioctl(ch->v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
		fprintf(stderr, "Ch%d: VIDIOC_STREAMON failed: %s\n",
			ch->index, strerror(errno));
		return -1;
	}

	printf("Ch%d: streaming started\n", ch->index);
	return 0;
}

static void channel_teardown_v4l2(struct ep_channel *ch)
{
	if (ch->v4l2_fd < 0) return;

	enum v4l2_buf_type type = ch->buf_type;
	xioctl(ch->v4l2_fd, VIDIOC_STREAMOFF, &type);

	for (unsigned int i = 0; i < ch->num_buffers; i++) {
		struct ep_buffer *buf = &ch->buffers[i];
		if (buf->dmabuf_fd >= 0)
			close(buf->dmabuf_fd);
		if (buf->mmap_addr && buf->mmap_addr != MAP_FAILED)
			munmap(buf->mmap_addr, buf->mmap_length);
	}

	close(ch->v4l2_fd);
	ch->v4l2_fd = -1;
}

/* ----------------------------------------------------------------
 * Memory barrier for ARM (endpoint is Cortex-A78)
 * ---------------------------------------------------------------- */

static inline void ep_wmb(void)
{
#if defined(__aarch64__)
	__asm__ volatile("dsb st" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("sfence" ::: "memory");
#else
	__sync_synchronize();
#endif
}

/* ----------------------------------------------------------------
 * Per-channel capture thread
 * ---------------------------------------------------------------- */

static void *channel_thread(void *arg)
{
	struct ep_channel *ch = arg;
	struct ep_proxy *proxy = (struct ep_proxy *)
		((char *)ch - offsetof(struct ep_proxy, channels) -
		 ch->index * sizeof(struct ep_channel));
	/* Re-derive proxy pointer from channel array position */
	proxy = (struct ep_proxy *)((char *)ch - offsetof(struct ep_proxy, channels[ch->index]));

	printf("Ch%d: capture thread started\n", ch->index);

	while (ch->running && g_running) {
		/* Dequeue a completed frame */
		struct v4l2_plane planes[1] = {0};
		struct v4l2_buffer vbuf = {0};
		vbuf.type = ch->buf_type;
		vbuf.memory = V4L2_MEMORY_MMAP;
		if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			vbuf.length = 1;
			vbuf.m.planes = planes;
		}

		if (xioctl(ch->v4l2_fd, VIDIOC_DQBUF, &vbuf) < 0) {
			if (errno == EAGAIN) continue;
			if (!g_running) break;
			fprintf(stderr, "Ch%d: VIDIOC_DQBUF failed: %s\n",
				ch->index, strerror(errno));
			break;
		}

		unsigned int buf_idx = vbuf.index;
		struct ep_buffer *buf = &ch->buffers[buf_idx];
		ch->frame_seq++;

		/*
		 * Stamp the in-band watermark into the first bytes of the
		 * frame buffer (offset 0).  The ep_wmb() before VALID (below)
		 * also orders this store, ensuring it reaches DDR before the
		 * host can DMA the frame.  Overwrites the top-left pixels.
		 */
		if (proxy->stamp_seq) {
			struct timespec wts;
			clock_gettime(CLOCK_MONOTONIC, &wts);
			uint32_t wm_seq = ch->frame_seq;

			/*
			 * Fault injection (detector self-test): on every Nth
			 * frame, rewind the *watermark* sequence so the host
			 * sees a backward jump.  The mailbox frame_sequence
			 * below is left correct/monotonic, so this reproduces
			 * the real bug's fingerprint -- stale content under
			 * good-looking metadata -- and proves the host decode
			 * actually flags it.
			 */
			if (proxy->fault_inject &&
			    ch->frame_seq > EP_FAULT_REWIND &&
			    (ch->frame_seq % proxy->fault_inject) == 0) {
				wm_seq = ch->frame_seq - EP_FAULT_REWIND;
				fprintf(stderr,
					"Ch%d: FAULT-INJECT seq %u -> wm %u\n",
					ch->index, ch->frame_seq, wm_seq);
			}

			struct mdb5_frame_wm wm = {
				.magic            = MDB5_FRAME_WM_MAGIC,
				.ch_index         = (uint32_t)ch->index,
				.frame_seq        = wm_seq,
				.reserved         = 0,
				.capture_ktime_ns = (uint64_t)wts.tv_sec * 1000000000ULL
						    + (uint64_t)wts.tv_nsec,
			};
			memcpy(buf->mmap_addr, &wm, sizeof(wm));
		}

		/* Write frame metadata to BRAM mailbox */
		volatile struct mdb5_mbox_slot *mbox = ch->mbox;

		mbox->frame_addr_lo = (uint32_t)(buf->phys_addr & 0xFFFFFFFF);
		mbox->frame_addr_hi = (uint32_t)(buf->phys_addr >> 32);
		mbox->frame_size    = ch->sizeimage;
		mbox->frame_sequence = ch->frame_seq;

		/* Memory barrier before setting VALID */
		ep_wmb();

		mbox->frame_status = MDB5_FRAME_VALID | MDB5_FRAME_STREAMING;

		/*
		 * Trigger MSI-X interrupt to host via the cross-PF doorbell
		 * (PF0_PORT_LOGIC_MSIX_DOORBELL_OFF): bits[28:24] select the
		 * target PF, bits[10:0] select the vector within that PF's
		 * own table. The new 4-PF split gives PF1 (camera) its own
		 * MSI-X table allocated as 4 dedicated frame-notify vectors
		 * starting at 0 -- no eDMA-vector offset to reserve, unlike
		 * the old single-PF design where this register only ever
		 * had to address PF0's own combined table (hence no PF field
		 * needed there). Hardcoded to PF1 (function 1); the old
		 * single-PF design is not carried forward.
		 */
		*proxy->msix_reg = (1u << 24) | (uint32_t)ch->index;

		/* Wait for host ACK (poll with timeout) */
		struct timespec ts_start, ts_now;
		clock_gettime(CLOCK_MONOTONIC, &ts_start);

		while (g_running && ch->running) {
			if (mbox->host_ack == ch->frame_seq)
				break;

			clock_gettime(CLOCK_MONOTONIC, &ts_now);
			long elapsed_ms = (ts_now.tv_sec - ts_start.tv_sec) * 1000 +
					  (ts_now.tv_nsec - ts_start.tv_nsec) / 1000000;
			if (elapsed_ms >= (long)proxy->ack_timeout_ms) {
				/* Timeout - host didn't ACK. Drop frame and continue. */
				break;
			}
			usleep(50); /* Brief yield */
		}

		/* Clear VALID flag */
		mbox->frame_status = MDB5_FRAME_STREAMING;

		/* Requeue the buffer */
		struct v4l2_plane qplanes[1] = {0};
		struct v4l2_buffer qbuf = {0};
		qbuf.type = ch->buf_type;
		qbuf.memory = V4L2_MEMORY_MMAP;
		qbuf.index = buf_idx;
		if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			qbuf.length = 1;
			qbuf.m.planes = qplanes;
		}

		if (xioctl(ch->v4l2_fd, VIDIOC_QBUF, &qbuf) < 0) {
			fprintf(stderr, "Ch%d: VIDIOC_QBUF %u failed: %s\n",
				ch->index, buf_idx, strerror(errno));
			break;
		}

		/* Low-latency: drain stale frames queued during host DMA */
		if (proxy->low_latency) {
			int flags = fcntl(ch->v4l2_fd, F_GETFL);
			fcntl(ch->v4l2_fd, F_SETFL, flags | O_NONBLOCK);

			unsigned int drained = 0;
			for (;;) {
				struct v4l2_plane dp[1] = {0};
				struct v4l2_buffer db = {0};
				db.type = ch->buf_type;
				db.memory = V4L2_MEMORY_MMAP;
				if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
					db.length = 1;
					db.m.planes = dp;
				}

				if (xioctl(ch->v4l2_fd, VIDIOC_DQBUF, &db) < 0)
					break; /* EAGAIN — no more completed */

				/* Immediately requeue without sending to host */
				struct v4l2_plane rp[1] = {0};
				struct v4l2_buffer rb = {0};
				rb.type = ch->buf_type;
				rb.memory = V4L2_MEMORY_MMAP;
				rb.index = db.index;
				if (ch->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
					rb.length = 1;
					rb.m.planes = rp;
				}
				xioctl(ch->v4l2_fd, VIDIOC_QBUF, &rb);
				drained++;
			}

			fcntl(ch->v4l2_fd, F_SETFL, flags);

			if (drained)
				printf("Ch%d: low-latency drained %u stale frame(s)\n",
				       ch->index, drained);
		}
	}

	printf("Ch%d: capture thread exiting\n", ch->index);
	return NULL;
}

/* ----------------------------------------------------------------
 * Hardware memory mapping
 * ---------------------------------------------------------------- */

static int map_hardware(struct ep_proxy *proxy)
{
	proxy->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (proxy->mem_fd < 0) {
		perror("Failed to open /dev/mem (need root)");
		return -1;
	}

	/* Map BRAM */
	size_t bram_map_size = MDB5_BRAM_SIZE;
	uint64_t bram_phys = MDB5_EP_BRAM_PHYS;
	/* Align to page boundary */
	uint64_t bram_page = bram_phys & ~(PAGE_SIZE - 1);
	size_t bram_offset = bram_phys - bram_page;

	void *bram_map = mmap(NULL, bram_map_size + bram_offset,
			      PROT_READ | PROT_WRITE, MAP_SHARED,
			      proxy->mem_fd, bram_page);
	if (bram_map == MAP_FAILED) {
		perror("Failed to mmap BRAM");
		return -1;
	}
	proxy->bram = (volatile void *)((char *)bram_map + bram_offset);
	proxy->bram_header = (volatile struct mdb5_mbox_header *)proxy->bram;

	/* Map MSI-X trigger register */
	uint64_t msix_phys = MDB5_EP_MSIX_TRIGGER_PHYS;
	uint64_t msix_page = msix_phys & ~(PAGE_SIZE - 1);
	size_t msix_offset = msix_phys - msix_page;

	void *msix_map = mmap(NULL, PAGE_SIZE,
			      PROT_READ | PROT_WRITE, MAP_SHARED,
			      proxy->mem_fd, msix_page);
	if (msix_map == MAP_FAILED) {
		perror("Failed to mmap MSI-X register");
		return -1;
	}
	proxy->msix_reg = (volatile uint32_t *)((char *)msix_map + msix_offset);

	printf("BRAM mapped at %p, MSI-X trigger at %p\n",
	       (void *)proxy->bram, (void *)proxy->msix_reg);
	return 0;
}

/* ----------------------------------------------------------------
 * BRAM initialization
 * ---------------------------------------------------------------- */

static void init_bram(struct ep_proxy *proxy)
{
	/* Clear entire BRAM mailbox area */
	volatile uint32_t *p = (volatile uint32_t *)proxy->bram;
	size_t words = (MDB5_MBOX_HEADER_SIZE +
			proxy->num_channels * MDB5_MBOX_SLOT_SIZE) / sizeof(uint32_t);
	for (size_t i = 0; i < words; i++)
		p[i] = 0;

	/* Write per-channel static format metadata */
	for (int i = 0; i < proxy->num_channels; i++) {
		struct ep_channel *ch = &proxy->channels[i];
		volatile struct mdb5_mbox_slot *slot =
			(volatile struct mdb5_mbox_slot *)
			((char *)proxy->bram + MDB5_MBOX_SLOT_OFFSET(i));
		ch->mbox = slot;

		slot->width = ch->width;
		slot->height = ch->height;
		slot->pixel_format = ch->pixelformat;
		slot->bytesperline = ch->bytesperline;
		slot->frame_size = ch->sizeimage;
		slot->frame_status = MDB5_FRAME_STREAMING;
	}

	ep_wmb();

	/* Write header last - host will poll for magic */
	proxy->bram_header->num_channels = proxy->num_channels;
	proxy->bram_header->ep_status = MDB5_EP_STATUS_READY;
	ep_wmb();
	proxy->bram_header->magic = MDB5_MAGIC;

	printf("BRAM mailbox initialized: %d channels, magic=0x%08x\n",
	       proxy->num_channels, MDB5_MAGIC);
}

/* ----------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -j PATH   JSON config file (default: %s)\n"
		"  -n        Disable in-band frame watermark stamping\n"
		"            (stamping is ON by default; it overwrites the\n"
		"             first %zu bytes / top-left of each frame)\n"
		"  -b N      V4L2 buffers per channel (default %d, max %d)\n"
		"  -t MS     Host ACK poll timeout in ms (default %d)\n"
		"  -F N      Detector self-test: stamp a stale watermark every\n"
		"            Nth frame (rewound by %d) so the host MUST log an\n"
		"            inversion. 0=off (default). TEST ONLY -- corrupts\n"
		"            frame ordering on purpose.\n"
		"  -h        Show this help\n"
		"\n"
		"CLI flags override the corresponding JSON keys\n"
		"(stamp_seq, num_buffers, ack_timeout_ms, fault_inject).\n",
		prog, DEFAULT_CONFIG_PATH, sizeof(struct mdb5_frame_wm),
		EP_DEFAULT_BUFS, EP_MAX_BUFFERS, HOST_ACK_TIMEOUT_MS,
		EP_FAULT_REWIND);
}

int main(int argc, char **argv)
{
	struct ep_proxy proxy = {0};
	strncpy(proxy.config_path, DEFAULT_CONFIG_PATH, sizeof(proxy.config_path) - 1);

	/* Defaults (may be overridden by JSON config, then by CLI flags) */
	proxy.stamp_seq = true;
	proxy.num_buffers = EP_DEFAULT_BUFS;
	proxy.ack_timeout_ms = HOST_ACK_TIMEOUT_MS;

	/*
	 * CLI flags take precedence over the JSON config, so remember which
	 * ones were set explicitly and re-apply them after parse_config().
	 */
	bool cli_no_stamp = false, cli_bufs = false, cli_ack = false;
	bool cli_fault = false;
	unsigned int cli_num_buffers = 0, cli_ack_timeout_ms = 0;
	unsigned int cli_fault_inject = 0;

	int opt;
	while ((opt = getopt(argc, argv, "j:nb:t:F:h")) != -1) {
		switch (opt) {
		case 'j':
			strncpy(proxy.config_path, optarg, sizeof(proxy.config_path) - 1);
			break;
		case 'n':
			cli_no_stamp = true;
			break;
		case 'b':
			cli_bufs = true;
			cli_num_buffers = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 't':
			cli_ack = true;
			cli_ack_timeout_ms = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'F':
			cli_fault = true;
			cli_fault_inject = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'h':
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	printf("MDB5 Endpoint Proxy starting\n");
	printf("Config: %s\n", proxy.config_path);

	/* Install signal handlers */
	struct sigaction sa = {0};
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Parse configuration */
	if (parse_config(&proxy, proxy.config_path) < 0)
		return 1;

	/* CLI flags override the JSON config */
	if (cli_no_stamp)
		proxy.stamp_seq = false;
	if (cli_bufs)
		proxy.num_buffers = cli_num_buffers;
	if (cli_ack)
		proxy.ack_timeout_ms = cli_ack_timeout_ms;
	if (cli_fault)
		proxy.fault_inject = cli_fault_inject;

	/* Clamp / validate */
	if (proxy.num_buffers < 2)
		proxy.num_buffers = 2;
	if (proxy.num_buffers > EP_MAX_BUFFERS)
		proxy.num_buffers = EP_MAX_BUFFERS;

	if (proxy.num_channels == 0) {
		fprintf(stderr, "No channels configured\n");
		return 1;
	}
	if (proxy.low_latency)
		printf("Low-latency mode enabled (stale frames will be dropped)\n");
	printf("stamp_seq=%s num_buffers=%u ack_timeout_ms=%u\n",
	       proxy.stamp_seq ? "on" : "off",
	       proxy.num_buffers, proxy.ack_timeout_ms);
	if (proxy.fault_inject)
		fprintf(stderr,
			"*** FAULT INJECTION ACTIVE: stale watermark every %u "
			"frames (TEST BUILD -- ordering deliberately corrupted) ***\n",
			proxy.fault_inject);

	/* Map hardware registers */
	if (map_hardware(&proxy) < 0)
		return 1;

	/* Set up V4L2 capture for each channel (skip failures) */
	int active = 0;
	for (int i = 0; i < proxy.num_channels; i++) {
		if (channel_setup_v4l2(&proxy.channels[i], proxy.mem_fd,
				       proxy.num_buffers) < 0) {
			fprintf(stderr, "Ch%d: setup failed, skipping\n", i);
			proxy.channels[i].v4l2_fd = -1;
		} else {
			active++;
		}
	}
	if (active == 0) {
		fprintf(stderr, "No channels started successfully\n");
		return 1;
	}
	printf("%d of %d channels active\n", active, proxy.num_channels);

	/* Initialize BRAM mailbox (sets magic so host can discover us) */
	init_bram(&proxy);

	/* Spawn per-channel capture threads (skip failed channels) */
	for (int i = 0; i < proxy.num_channels; i++) {
		if (proxy.channels[i].v4l2_fd < 0)
			continue;
		proxy.channels[i].running = true;
		int ret = pthread_create(&proxy.channels[i].thread, NULL,
					 channel_thread, &proxy.channels[i]);
		if (ret) {
			fprintf(stderr, "Ch%d: pthread_create failed: %s\n",
				i, strerror(ret));
			proxy.channels[i].running = false;
		}
	}

	printf("All channels running. Press Ctrl-C to stop.\n");

	/* Wait for signal */
	while (g_running)
		usleep(100000);

	printf("\nShutting down...\n");

	/* Stop all channels */
	for (int i = 0; i < proxy.num_channels; i++)
		proxy.channels[i].running = false;

	/* Join threads */
	for (int i = 0; i < proxy.num_channels; i++) {
		if (proxy.channels[i].thread)
			pthread_join(proxy.channels[i].thread, NULL);
	}

	/* Clear BRAM magic so host knows we're gone */
	proxy.bram_header->magic = 0;
	proxy.bram_header->ep_status = 0;

	/* Teardown V4L2 */
	for (int i = 0; i < proxy.num_channels; i++)
		channel_teardown_v4l2(&proxy.channels[i]);

	close(proxy.mem_fd);
	printf("Endpoint proxy stopped\n");
	return 0;
}
