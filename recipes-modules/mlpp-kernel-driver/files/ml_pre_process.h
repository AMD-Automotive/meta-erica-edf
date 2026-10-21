// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

#ifndef __ML_PRE_PROCESS_H__
#define __ML_PRE_PROCESS_H__

#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/aer.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/rwsem.h>
#include <linux/cpumask.h>
#include <linux/cma.h>
#include <linux/kfifo.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/videodev2.h>
#include <linux/capability.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/xarray.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/cma.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h> // New include for platform drivers
#include <linux/cdev.h> // Add this for character device support
#include <linux/delay.h>
#include <linux/interrupt.h> // For ISR support
#include <linux/wait.h> // For wait queues
#include <linux/workqueue.h> // For deferred frame dispatch
#include <linux/completion.h> // For completion synchronization
#include <linux/of.h> // For device tree support
#include <linux/of_platform.h> // For device tree platform support
#include <linux/dma-buf.h> // For DMABUF support
#include <linux/dma-fence.h> // For DMA fence operations
#include <linux/file.h> // For get_unused_fd_flags/fd_install/put_unused_fd
#include <linux/scatterlist.h> // For sg_table helpers
#include <linux/ktime.h>

// Module parameters (defined in main .c file)
extern int poll_mode;
extern int debug;
extern int performance_ts;

#define DEBUG_PRINT(format, ...) \
        if (debug) { \
            pr_info(format, ##__VA_ARGS__); \
        } \

#define PERF_PRINT(format, ...) \
        if (performance_ts) { \
            printk(KERN_INFO format, ##__VA_ARGS__); \
        } \

#define ONE_MB_BUFFER_SIZE (1024 * 1024) // 1MB buffer

/* Per-device framebuffer depth. Output devices (5..19) use a MLPP_OUT_NBUF-deep
 * ring so the hardware/ISR producer can run ahead of a slow userspace reader by
 * up to MLPP_OUT_NBUF-1 completed frames. Input devices (0..4) keep a simple
 * MLPP_IN_NBUF ping-pong so the next frame can be filled while the current one
 * is processed. MLPP_MAX_NBUF sizes the storage arrays. */
#define MLPP_IN_NBUF   2
/* Maximum W1C drain passes in one hardirq entry.
 *
 * The drain loops used to be unbounded: `while ((status = rdreg(ISR) & mask))`.
 * If ANY bit in the mask is not genuinely write-1-to-clear (level-follows-
 * condition, or read-only status), that bit never clears and the loop spins
 * forever in hardirq with interrupts off -- RCU stalls, dead CPU. Bound it,
 * and mask IER if the bound is ever hit so the machine survives. */
#define MLPP_ISR_DRAIN_MAX 16

#define MLPP_OUT_NBUF  4
#define MLPP_MAX_NBUF  4
#define MLPP_FIRST_OUT_DEV 5   /* devices 0..4 are inputs, 5..19 are outputs */

/* Hard cap on how many exported DMABUFs may be alive for one output device
 * before ml_pp_export_dmabuf() starts refusing with -EMFILE. The kernel cannot
 * revoke an fd already handed to userspace, so this backpressure is the only
 * bound on a consumer that never close()s its exports. Set well above the ring
 * depth so normal multi-buffering is never rejected. */
#define MLPP_MAX_LIVE_EXPORTS (4 * MLPP_OUT_NBUF)

/* Number of framebuffers allocated for a device (inputs ping-pong, outputs ring). */
static inline int mlpp_dev_nbuf(int dev)
{
	return (dev < MLPP_FIRST_OUT_DEV) ? MLPP_IN_NBUF : MLPP_OUT_NBUF;
}

/* Bytes of a slot that are safe to DMA into or map out.
 *
 * Each slot is allocated as (cma_alloc_size + MLPP_SLOT_SLACK) and then shifted
 * forward by up to 63 bytes for 64-byte alignment, so the usable payload is the
 * allocation minus the worst-case alignment shift. Callers must bound-check
 * against this, not against cma_alloc_size. */
#define MLPP_SLOT_SLACK   8
#define MLPP_SLOT_ALIGN   64
size_t mlpp_slot_payload(int dev);

/* Upper bound on a single output frame (bytes). Sized well above the largest
 * real configuration (3840x2160x4 = 31.6 MB) purely to keep untrusted
 * width/height from overflowing the size arithmetic. */
#define MLPP_MAX_OUT_BYTES  (64u * 1024u * 1024u)

#define MLPP_COLOR_RGB888 0
#define MLPP_COLOR_YUV888M 1
#define MLPP_COLOR_UYVY_422 2
#define MLPP_COLOR_UYVY_420 3
#define MLPP_COLOR_BGR888 4

/* Output-format selector for ml_pp_output_params.color_out:
 *   0 = RGB/YUV444P (3 B/px)   1 = YUV444M (3 B/px)
 *   2 = RGBX/YUVX   (4 B/px)   3 = Y only  (1 B/px)
 *   4 = NV12 (1.5 B/px, display pipe only; requires CSC bypass / YUV input)
 */
#define MLPP_COLOR_OUT_RGB 0
#define MLPP_COLOR_OUT_YUV444M 1
#define MLPP_COLOR_OUT_RGBX 2
#define MLPP_COLOR_OUT_Y 3
#define MLPP_COLOR_OUT_NV12 4

// 0 - RGB/YUV444P , 1 - YUV444M, 2 - RGBX/YUV444(A), 3 - Y Only (Grey)

#define DRIVER_NAME "ml_pre_process"
#define DEVICE_NAME "ml_pre_process"

/* ================================================================
 * NEW mlpp-v2 hardware register model (replaces the old VPSS blocks)
 *
 * The IP exposes TWO AXI4-Lite control windows per engine:
 *   RD region : input-DMA config (frame size, base, GO) + read-done IRQ
 *   WR region : per-ROI processing/CSC config + write-done IRQs
 * There are two engines: FVC (7 ROIs) and SVC (2 ROIs). Both share the same
 * register layout below; the driver maps four windows by device-tree reg-name:
 *   "fvc_rd", "fvc_wr", "svc_rd", "svc_wr".
 * ================================================================ */
/* RD region */
#define MLPP_CTRL            0x00
#define   MLPP_CTRL_ENABLE   BIT(0)
#define   MLPP_CTRL_GO       BIT(1)   /* write 1 to start one frame; self-clears */
#define MLPP_INPUT_HSIZE     0x08
#define MLPP_INPUT_VSIZE     0x0C
#define MLPP_IN_DMA_BASE_LO  0x10
#define MLPP_IN_DMA_BASE_HI  0x14
/* WR region */
#define MLPP_CSC_BYPASS      0x18     /* bit r = ROI r CSC bypass */
/* Per-region interrupt status/enable (W1C status) and version */
#define MLPP_ISR             0x50
#define MLPP_IER             0x54
#define MLPP_VERSION         0xFC
#define MLPP_RD_ISR_MASK     0x1u     /* RD region: bit0 = input read done */

/* Per-ROI block in the WR region (stride 0x50; base = 0x100 + r*0x50). */
#define MLPP_ROI_BASE(r)     (0x100 + (r) * 0x50)
#define MLPP_ROI_START_XY    0x00     /* (start_y<<16)|start_x */
#define MLPP_ROI_OUT_WH      0x04     /* (out_h<<16)|out_w */
#define MLPP_ROI_STEP_X      0x08     /* Q16.16 = (roi_w<<16)/out_w */
#define MLPP_ROI_STEP_Y      0x0C
#define MLPP_ROI_SUB         0x10     /* (sub_rcr<<18)|(sub_bcb<<9)|sub_gy */
#define MLPP_ROI_CFG         0x14
#define MLPP_ROI_OBASE_LO    0x18
#define MLPP_ROI_OBASE_HI    0x1C
#define MLPP_ROI_CSC_COEF(i)   (0x20 + (i) * 4)   /* i = 0..8 */
#define MLPP_ROI_CSC_OFFSET(i) (0x44 + (i) * 4)   /* i = 0..2 */

/* Logical engine indices. */
#define MLPP_ENGINE_FVC   0
#define MLPP_ENGINE_SVC   1
#define MLPP_NUM_ENGINES  2
#define MLPP_FVC_NROI     7
#define MLPP_SVC_NROI     2
#define MLPP_SVC_NSRC     4     /* svc0..3 cameras share the single SVC engine */

/* One queued frame for a multi-source engine (SVC). At most one is outstanding
 * per source (camera): a write to a busy source blocks; writes to different
 * sources are queued independently and dispatched as the engine frees. */
struct mlpp_frame_req {
	bool        pending;                 /* queued or in-flight for this source */
	u32         in_w;
	u32         in_h;
	dma_addr_t  in_base;
	int         out_devs[MLPP_SVC_NROI];
	int         nroi;
};

/* One processing engine (FVC or SVC): its two AXI-Lite windows, completion
 * IRQs and the software done-mask assembled by the ISRs. */
struct mlpp_engine {
	void __iomem      *rd;          /* input-DMA control window */
	void __iomem      *wr;          /* processing/output control window */
	int                irq_rd;
	int                irq_wr;
	u8                 nroi;        /* active output ROIs */
	wait_queue_head_t  wq;          /* woken by the completion ISRs */
	spinlock_t         lock;        /* protects done_mask/busy/inflight */
	u32                done_mask;   /* bit0 = read, bits[nroi:1] = ROI writes */
	struct mutex       op_lock;     /* serialises one frame at a time */

	/* Asynchronous (non-blocking) frame pipeline: a kicked frame runs in the
	 * background; the completion ISR publishes the outputs and signals
	 * frame_done. A subsequent kick waits on the PREVIOUS frame only, so the
	 * producer can prepare the next input while the hardware runs. */
	bool               busy;        /* a frame is currently in flight */
	struct completion  frame_done;  /* completed by the ISR when done */
	int                inflight_devs[MLPP_FVC_NROI]; /* output devs of in-flight frame */
	int                inflight_slots[MLPP_FVC_NROI];/* ring slot each output writes to */
	int                inflight_nroi;

	/* Multi-source (SVC) non-blocking dispatch: each camera has at most one
	 * outstanding frame. Writes to different cameras never serialise; a
	 * second write to the SAME camera blocks until its frame completes. */
	bool                  multi_src;              /* engine uses the queue (SVC) */
	/* IN_DMA_BASE write/read-back verification (see mlpp_write_in_base_verify).
	 * base_verify_writes counts every programmed input base; base_verify_fail
	 * counts those whose read-back did not match what we wrote. On healthy
	 * hardware fail must stay 0 forever. */
	atomic_t              base_verify_writes;
	atomic_t              base_verify_fail;

	struct mlpp_frame_req src_req[MLPP_SVC_NSRC]; /* one slot per camera */
	struct completion     src_done[MLPP_SVC_NSRC];/* fires when a source drains */
	int                   cur_src;               /* source of in-flight frame, else -1 */
	int                   rr_next;               /* round-robin dispatch cursor */
	struct work_struct    dispatch_work;         /* drains the queue in process ctx */

	/* ---- IRQ diagnostics (free-running; read via debugfs) ---------------
	 * /proc/interrupts cannot be sampled while the pipeline is wedged, so
	 * count in-driver. These answer the one question that matters after a
	 * stall: did the IP stop asserting, or did we mishandle an assert?
	 *   entries   - hardirq invocations that saw at least one status bit
	 *   none      - invocations that saw nothing (shared/cascaded lines)
	 *   drain_max - times the bounded drain hit its limit (a stuck bit) */
	atomic_t           irq_rd_entries;
	atomic_t           irq_wr_entries;
	atomic_t           irq_rd_none;
	atomic_t           irq_wr_none;
	atomic_t           irq_drain_max;
};

static const char mlpp_device_names[20] [20] = {
	"mlpp_ffc_in", "mlpp_svc0_in", "mlpp_svc1_in", "mlpp_svc2_in",
	"mlpp_svc3_in", "mlpp_ffc_out0", "mlpp_ffc_out1", "mlpp_ffc_out2",
	"mlpp_ffc_out3", "mlpp_ffc_out4", "mlpp_ffc_out5", "mlpp_ffc_display",
	"mlpp_svc0_out", "mlpp_svc1_out", "mlpp_svc2_out", "mlpp_svc3_out",
	"mlpp_svc0_display", "mlpp_svc1_display", "mlpp_svc2_display", "mlpp_svc3_display"
};

static const int mlpp_device_cma_sizes[20] = {
	26, 10, 10, 10,
	10, 1, 1, 2,
	4, 1, 1, 8,
	2, 2, 2, 2,
	8, 8, 8, 8
};

// Global variables (defined in the main C file)
extern int verify_base;   /* 0=off 1=on-mismatch 2=every frame */
extern ssize_t input_byte_count[20];
extern ssize_t output_byte_count[20];
extern u32 image_in_size;
extern unsigned long timeout_33ms;

// External declaration of file operations structure array
extern const struct file_operations ml_pp_fops[20];

// ML preprocessing output parameters structure
struct ml_pp_output_params {
    /*00*/ u16 width_out;
    /*02*/ u16 height_out;
    /*04*/ u16 crop_xstart;
    /*06*/ u16 crop_xstop;
    /*08*/ u16 crop_ystart;
    /*0A*/ u16 crop_ystop;
    /*0C*/ u8 sub_gy;
    /*0D*/ u8 sub_bcb;
    /*0E*/ u8 sub_rcr;
    /*10*/ u16 color_from_csc;	// 0 - RGB , 1 - YUV444M, 2 - BGR
    /*12*/ u16 color_out;		// 0 - RGB/YUV444P , 1 - YUV444M, 2 - RGBX/YUV444(A), 3 - Y Only (Grey)
    /*14*/ int scale_y; 		// Will round to the nearest power of 2
    /*18*/ int scale_cb;
    /*1C*/ int scale_cr;
} __packed;

struct ml_pp_output_params_stored {
    /*00*/ u16 width_out;
    /*04*/ u16 height_out;
    /*08*/ u16 crop_xstart;
    /*0c*/ u16 crop_xstop;
    /*10*/ u16 crop_ystart;
    /*14*/ u16 crop_ystop;
    /*16*/ u32 subtract_value;
    /*2A*/ u32 scale_value;
    /*2C*/ u16 color_from_csc;
    /*2E*/ u16 color_out;
} __packed;


// ML preprocessing input parameters structure
struct ml_pp_input_params {
    /*00*/ u16 width_in;
    /*04*/ u16 height_in;
    /*08*/ u16 color_in; // 0 - RGB/YUV444P , 1 - YUV444M, 2 - UYVY 4:2:2, 3 - NV12
} __packed;

// ML preprocessing DMABUF parameters structure
struct ml_pp_dmabuf_params {
    int dmabuf_fd;          // DMABUF file descriptor from V4L2
    u32 offset;             // Offset into the DMABUF
    u32 size;               // Size of data in bytes
    u32 frame_sequence;     // Frame sequence number for synchronization
} __packed;

// External declarations for pipe parameter arrays (defined in main C file)
extern struct ml_pp_output_params_stored mlpp_pipe0[7];
extern struct ml_pp_output_params_stored mlpp_pipe1[3]; 
extern struct ml_pp_output_params_stored mlpp_pipe2[5]; // using 6

extern struct ml_pp_input_params mlpp_input_params[5];

// Lookup table: Maps device index (0-19) to corresponding pipe parameters
// Returns pointer to the appropriate parameter structure for each device
extern struct ml_pp_output_params_stored* mlpp_params_lookup[20];

/* ---------------- Imported-DMABUF cache -----------------------------------
 *
 * The producer (VCU decoder via GStreamer, or V4L2 for the ISP path) recycles a
 * small fixed pool of buffers -- 8 per channel for the decoder. Before this
 * cache every write() to an input device did a full
 *
 *     cleanup(unmap+detach+put) -> dma_buf_get -> attach -> map_attachment
 *
 * on EVERY frame, even though the very same dma_buf came back moments later.
 * That is ~30 unmap/remap cycles a second per channel, and it means the window
 * between "we released the mapping" and "the engine is definitely finished
 * with it" is re-opened once per frame, forever.
 *
 * The cache keys on `struct dma_buf *`, NOT on the fd. File descriptors are
 * recycled aggressively by userspace: the same fd number routinely refers to a
 * different buffer minutes later, and the same buffer can arrive under a
 * different fd. Keying on the fd would therefore both miss (same buffer, new
 * fd) and, far worse, FALSELY HIT (new buffer, recycled fd) -- programming the
 * engine with a stale physical address. The dma_buf pointer is stable for the
 * lifetime of the buffer, and we hold a reference to each cached entry, so the
 * pointer cannot be freed and re-used for a different buffer while we cache it.
 *
 * Holding that reference is the point, not a side effect: it guarantees the
 * exporter cannot free or recycle the backing pages while MLPP still has the
 * address programmed.
 */
#define MLPP_DMABUF_CACHE_MAX 20   /* >= decoder pool (8) + in-flight slack */

struct mlpp_dmabuf_cache_entry {
	struct dma_buf *dmabuf;              /* cache key; we hold a reference  */
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	size_t size;                         /* clamped effective payload size  */
	u64 last_used;                       /* for LRU eviction                */
	bool valid;
};

// ML preprocessing private structure definition
struct ml_pp_priv {
	struct device *dev;
	// Framebuffer ring: slots [dev][0..mlpp_dev_nbuf(dev)-1].
	// Inputs use slots 0/1 (ping-pong); outputs use all MLPP_OUT_NBUF slots.
	void *cpu_addr_fb[20][MLPP_MAX_NBUF];       // aligned CPU address per slot
	void *cpu_addr_fb_orig[20][MLPP_MAX_NBUF];  // original CPU address for cleanup
	dma_addr_t dma_handle_fb[20][MLPP_MAX_NBUF];      // aligned DMA handle per slot
	dma_addr_t dma_handle_fb_orig[20][MLPP_MAX_NBUF]; // original DMA handle for cleanup
	// Buffer management
	int current_write_buffer[20]; // input ping-pong index (slot 0/1)
	bool buffer_written[20][MLPP_MAX_NBUF]; // output ring: slot filled & ready to read
	int cma_alloc_size[20];
	int wr_idx[20]; // output ring: next slot the producer (HW/ISR) fills
	int rd_idx[20]; // output ring: next slot the consumer (read) drains

	bool read_ready[20];
	struct completion read_complete[20];
	
	// DMABUF support
	struct dma_buf *dmabuf[5];           // Current DMABUF object
	struct dma_buf_attachment *dmabuf_attach[5]; // DMABUF attachment
	struct sg_table *dmabuf_sgt[5];     // Scatter-gather table
	dma_addr_t dmabuf_dma_addr[5];      // DMA address of DMABUF
	/* Effective payload size for the imported buffer. This is OUR view of the
	 * buffer, clamped to the exporter's real dma_buf->size. NEVER write to the
	 * exporter's dma_buf->size -- it is owned by the exporter and shared with
	 * every other importer of the same buffer. */
	size_t dmabuf_size[5];
	dma_addr_t dmabuf_plane1_addr[5];   // DMABUF plane 1 DMA address
	dma_addr_t dmabuf_plane2_addr[5];   // DMABUF plane 2 DMA address
	dma_addr_t dmabuf_plane3_addr[5];   // DMABUF plane 3 DMA address
	bool using_dmabuf[5];               // Flag to indicate DMABUF mode
	atomic_t active_opens[5];           // Count of active device opens

	/* Serialises import/replace/teardown of the imported input DMABUFs.
	 * A mutex (not a spinlock): dma_buf_attach/map/detach/put may all sleep.
	 * lock[] below still protects the short pointer-snapshot critical sections
	 * that the ISR-adjacent paths need. */
	struct mutex dmabuf_lock[5];

	/* Imported-DMABUF cache, one set per input. Protected by
	 * dmabuf_lock[input] -- every access is from the write()/release() path,
	 * never from IRQ context. */
	struct mlpp_dmabuf_cache_entry dmabuf_cache[5][MLPP_DMABUF_CACHE_MAX];
	u64 dmabuf_cache_clock[5];           /* monotonic tick for LRU          */
	atomic_t dmabuf_cache_hits[5];
	atomic_t dmabuf_cache_misses[5];
	atomic_t dmabuf_cache_evictions[5];
	atomic_t dmabuf_cache_stale[5];      /* entry dropped: exporter released */

	/* Output ring slots handed to userspace as exported DMABUFs.
	 *
	 * ADVISORY ONLY. This says "userspace still has a mapping of this slot",
	 * and its sole consumers are mlpp_dev_has_pinned_slots() in the CMA resize
	 * (ml_pp_write) and remove paths, which must not FREE memory that is still
	 * mapped. The producer deliberately ignores it: making mlpp_output_reserve()
	 * skip pinned slots starved the ring and wedged readers on a 2s timeout.
	 * Overwriting a pinned slot yields a torn frame, never a use-after-free.
	 *
	 * Counts ONLY long-lived export pins. Transient readers use buf_sem below;
	 * mixing the two in one counter would let a momentary read fail a resize. */
	atomic_t slot_pinned[20][MLPP_MAX_NBUF];

	/* Guards the *existence* of a device's CMA slots against reallocation.
	 * Readers (ml_pp_read's copy_to_user, ml_pp_export_dmabuf's claim) take it
	 * for read; the resize in ml_pp_write takes it for write. This is what
	 * makes it safe to touch cpu_addr_fb[]/dma_handle_fb[] outside a spinlock
	 * across a sleeping copy. */
	struct rw_semaphore buf_sem[20];

	/* ---- diagnostic counters (free-running, never reset) ----------------
	 * Exported read-only through /sys/kernel/debug/mlpp/stats so a userspace
	 * watchdog can correlate a consumer stall with what the driver was doing.
	 * Plain atomics: incremented on hot paths, read only by debugfs. */
	atomic_t stat_frames_done[20];    /* completions published to the ring   */
	atomic_t stat_drops[20];          /* drop-oldest events (ring was full)  */
	atomic_t stat_wait_timeouts[20];  /* consumer waits that hit -ETIMEDOUT  */
	atomic_t stat_exports[20];        /* DMABUFs successfully exported       */
	atomic_t stat_export_released[20];/* exported DMABUFs released by user   */
	atomic_t stat_export_fail[20];    /* export attempts that failed         */

	// Per-engine lock (protects DMABUF state and engine bookkeeping)
	spinlock_t lock[3];

	// New mlpp-v2 hardware engines (FVC + SVC), mapped from device tree.
	struct mlpp_engine engine[MLPP_NUM_ENGINES];
};

// External declaration of global private structure
extern struct ml_pp_priv *global_priv;

// New mlpp-v2 hardware backend (vpss_helpers.c)
int mlpp_hw_map_and_request(struct platform_device *pdev, struct ml_pp_priv *priv);
void mlpp_hw_release(struct ml_pp_priv *priv);
int mlpp_process_fvc_frame(void);
int mlpp_process_svc_frame(int camera);
/* Block until a specific SVC camera's outstanding frame (if any) completes.
 * Blocks only on that camera; other cameras are unaffected. */
void mlpp_svc_wait_src_idle(int camera);
/* Current input ping-pong (slot 0/1) CPU address for an input device. */
void *mlpp_input_write_cpu(int dev);
/* Block until an engine's in-flight frame completes (engine idx = MLPP_ENGINE_*). */
void mlpp_wait_engine_idle(int engine_idx);

/* Output ring (depth MLPP_OUT_NBUF) consumer helpers, defined in vpss_helpers.c.
 * mlpp_output_wait_ready(): block until the slot at rd_idx[dev] holds a frame.
 * mlpp_output_consumed():   free that slot and advance rd_idx. (The producer
 *                           never blocks on a full ring -- it drops the oldest
 *                           frame instead -- so this wakes nobody.) */
int mlpp_output_wait_ready(int dev);
/* Same, with an explicit timeout in ms. Used by the DMABUF export retry, which
 * only needs to absorb a just-missed drop-oldest and should not add another
 * full 2s to a read() that is going to fail anyway. */
int mlpp_output_wait_ready_timeout(int dev, unsigned int timeout_ms);
void mlpp_output_consumed(int dev);

/* Number of exported DMABUFs currently alive for a device (exports minus
 * releases). A value that climbs without bound is a userspace fd leak. */
int mlpp_dev_live_exports(int dev);

/* debugfs: /sys/kernel/debug/mlpp/stats. Both are no-ops if debugfs is off. */
void mlpp_debugfs_init(void);
void mlpp_debugfs_exit(void);
/* Atomically pin the head ring slot and advance rd_idx, for DMABUF export.
 * Returns the pinned slot, or -1 if no frame is currently ready. */
int mlpp_output_claim_for_export(int dev);
/* Take/release exclusive control of the engine feeding this output device, and
 * quiesce it. Bracket any free/realloc of that device's CMA slots with these.
 * Both may sleep; the lock variant must not be called with a spinlock held. */
void mlpp_output_engine_lock(int dev);
void mlpp_output_engine_unlock(int dev);

// DMABUF functions
int ml_pp_import_dmabuf(struct ml_pp_priv *priv, int dmabuf_fd, int input_index, u32 force_size);
int ml_pp_use_dmabuf_as_input(struct ml_pp_priv *priv, int input_index);
/* Full teardown of an imported input DMABUF: unmap, detach, put. Safe to call
 * when nothing is imported. Must NOT be called from atomic context. */
void ml_pp_cleanup_dmabuf(struct ml_pp_priv *priv, int input_index);

/* True if any ring slot of an output device is currently handed out as an
 * exported DMABUF. Used to refuse CMA reallocation that would free memory
 * still referenced by userspace mappings. */
bool mlpp_dev_has_pinned_slots(int dev);

// Output ring / read helper functions
void* get_current_read_buffer_tracking_addr(int device_index);
int ml_pp_export_dmabuf(int device_index, char __user *buf);

// File operation functions
int ml_pp_open(struct inode *inode, struct file *file);
int ml_pp_release(struct inode *inode, struct file *file);
ssize_t ml_pp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t ml_pp_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

int mlpp_ffc_in_open(struct inode *inode, struct file *file);
int mlpp_ffc_in_release(struct inode *inode, struct file *file);
ssize_t mlpp_ffc_in_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t mlpp_ffc_in_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

int mlpp_svc_in_open(struct inode *inode, struct file *file);
int mlpp_svc_in_release(struct inode *inode, struct file *file);
ssize_t mlpp_svc_in_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t mlpp_svc_in_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);


#endif /* __ML_PRE_PROCESS_H__ */
