// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

#include <linux/version.h>
#include "ml_pre_process.h"

// Global timeout for read operations (33ms)
unsigned long timeout_33ms;



#define NUM_DEVICES 20

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LT Thomas");
MODULE_DESCRIPTION("A simple example for pipelining frmbuf_rd, crop, scale, cvc, frmbuf_wr");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS("DMA_BUF");
#else
MODULE_IMPORT_NS(DMA_BUF);
#endif

// Module parameters
int poll_mode = 0;
module_param(poll_mode, int, 0644);
MODULE_PARM_DESC(poll_mode, "Use polling mode instead of interrupts (0=interrupts, 1=polling)");

int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug output (0=disabled, 1=enabled)");

int performance_ts = 0; // TODO
module_param(performance_ts, int, 0644);
MODULE_PARM_DESC(performance_ts, "Emit PERF timestamps");

/*
 * Verify every MLPP_IN_DMA_BASE write by reading the register back.
 *
 * With no IOMMU on this platform the value written here is a raw physical
 * address; if the IP ever latches something different the engine DMAs from
 * the wrong place and nothing else in the system reports it.
 *
 *   0 = off (default)
 *   1 = print only on mismatch  -- cheap, safe to leave on during a soak
 *   2 = print every frame       -- verbose, ~150 lines/s across 5 channels
 *
 * Runtime-writable: echo 1 > /sys/module/ml_pre_process/parameters/verify_base
 */
int verify_base = 0;
module_param(verify_base, int, 0644);
MODULE_PARM_DESC(verify_base,
	"Read back MLPP_IN_DMA_BASE after each write (0=off 1=on-mismatch 2=verbose)");
MODULE_PARM_DESC(performance_ts, "Enable performance timestamps (0=disabled, 1=enabled)");

// Global pointer to the DMA buffers - moved here for early access
struct ml_pp_priv *global_priv = NULL;
struct class *ml_pre_process_class;
dev_t ml_pp_dev_num;
struct device *char_dev_parent;
struct cdev *ml_pp_cdevs;  // Array of cdevs for multiple devices
struct device **ml_pp_devices;  // Array to store device pointers

// Global variables definitions (declared as extern in header)
ssize_t input_byte_count[20];
ssize_t output_byte_count[20];
u32 image_in_size = 0;

// Forward declarations
static void ml_pp_cleanup_dmabuf_locked(struct ml_pp_priv *priv, int input_index);
static void ml_pp_unpublish_dmabuf_locked(struct ml_pp_priv *priv, int input_index);
static void mlpp_cache_drop_entry(struct ml_pp_priv *priv, int input_index, int slot);
void mlpp_cache_flush(struct ml_pp_priv *priv, int input_index);

struct ml_pp_input_params mlpp_input_params[5] = {
	[0] = { // FFC input
		.width_in = 3840, .height_in = 2160,
		.color_in = MLPP_COLOR_UYVY_420
	},
	[1] = { // SVC0 input
		.width_in = 1920, .height_in = 1536,
		.color_in = MLPP_COLOR_UYVY_420
	},
	[2] = { // SVC1 input
		.width_in = 1920, .height_in = 1536,
		.color_in = MLPP_COLOR_UYVY_420
	},
	[3] = { // SVC2 input
		.width_in = 1920, .height_in = 1536,
		.color_in = MLPP_COLOR_UYVY_420
	},
	[4] = { // SVC3 input
		.width_in = 1920, .height_in = 1536,
		.color_in = MLPP_COLOR_UYVY_420
	}
};

// The defaults are set using the old MLPPv1 implmenetation for now, there is only 2 pipelines currently
struct ml_pp_output_params_stored mlpp_pipe0[7] = {
	[0] = { // LSD ROI2
		.width_out = 640, .height_out = 288,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 0, .crop_ystop = 1404,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[1] = { // OD ROI1
		.width_out = 1024, .height_out = 448,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 0, .crop_ystop = 1890,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[2] = { // OD ROI2
		.width_out = 2048, .height_out = 384,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 675, .crop_ystop = 1485, // Check this
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[3] = { // LD ROI1
		.width_out = 640, .height_out = 320,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 864, .crop_ystop = 2160,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[4] = { // SVC 0
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[5] = { // SVC 1
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_OUT_RGBX
	},
	[6] = { // SVC 2
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_OUT_RGBX
	}
};

struct ml_pp_output_params_stored mlpp_pipe1[3] = {
	[0] = { // LSD ROI 1
		.width_out = 608, .height_out = 256,
		.crop_xstart = 1152, .crop_xstop = 2688, .crop_ystart = 792, .crop_ystop = 1368,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_YUV888M
	},
	[1] = { // SC ROI 1
		.width_out = 480, .height_out = 270,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 0, .crop_ystop = 2160,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_YUV888M
	},
	[2] = { // SVC 3
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_BGR888, .color_out = MLPP_COLOR_YUV888M
	}
};

struct ml_pp_output_params_stored mlpp_pipe2[5] = {
	[0] = { // FFC Display Out
		.width_out = 1280, .height_out = 1080,
		.crop_xstart = 0, .crop_xstop = 3840, .crop_ystart = 0, .crop_ystop = 2160,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_NV12
	},
	[1] = { // SVC 0 Display Out
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_NV12
	},
	[2] = { // SVC 1 Display Out
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_NV12
	},
	[3] = { // SVC 2 Display Out
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_NV12
	},
	[4] = { // SVC 3 Display Out
		.width_out = 640, .height_out = 512,
		.crop_xstart = 0, .crop_xstop = 1920, .crop_ystart = 0, .crop_ystop = 1536,
		.subtract_value = 0x80000000, .scale_value = 0x80000000,
		.color_from_csc = MLPP_COLOR_YUV888M, .color_out = MLPP_COLOR_OUT_NV12
	}
};

struct ml_pp_output_params_stored* mlpp_params_lookup[20] = {
	// First 5 elements are for input which has no parameters
	NULL, NULL, NULL, NULL, NULL,

	// FFC outs 0 to 5
	&mlpp_pipe1[0],  // FFC - LSD ROI 1 
	&mlpp_pipe0[0],  // FFC - LSD ROI 2
	&mlpp_pipe0[1],	 // FFC - OD ROI 1
	&mlpp_pipe0[2],  // FFC - OD ROI 2
	&mlpp_pipe0[3],  // FFC - LD ROI 1
	&mlpp_pipe1[1],  // FFC - SC ROI 1	
	&mlpp_pipe2[0],  // FFC Display Out
	&mlpp_pipe0[4],  // SVC 0 ROI
	&mlpp_pipe0[5],  // SVC 1 ROI
	&mlpp_pipe0[6],  // SVC 2 ROI
	&mlpp_pipe1[2],  // SVC 3 ROI
	&mlpp_pipe2[1],  // SVC 0 Display Out
	&mlpp_pipe2[2],  // SVC 1 Display Out  
	&mlpp_pipe2[3],  // SVC 2 Display Out
	&mlpp_pipe2[4],  // SVC 3 Display Out
	//&mlpp_pipe2[5]   // test Display Out
};

// Forward declaration of the private structure
// Forward declarations

// ================================================================
// Interrupt handling for the mlpp-v2 hardware lives in vpss_helpers.c
// (per-engine RD/WR completion ISRs). The old per-pipeline VPSS ISRs have
// been removed.
// ================================================================

/* Import and map an input DMABUF exported by another driver (e.g. the VCU).
 *
 * @force_size: optional userspace hint for the payload size. It is only ever
 *              used to SHRINK our view of the buffer; it is clamped to the
 *              exporter's real size and stored in priv->dmabuf_size[]. The
 *              exporter's dma_buf->size is never modified -- that field is
 *              owned by the exporter and shared with every other importer.
 *
 * Caller must NOT hold priv->dmabuf_lock[input_index]; this takes it. May sleep.
 */
/* ---------------- Imported-DMABUF cache ----------------------------------
 *
 * See the design note above struct mlpp_dmabuf_cache_entry in ml_pre_process.h.
 * All of these require priv->dmabuf_lock[input_index] to be held.
 */

/* Drop one cache entry: unmap, detach, release our dma_buf reference. */
static void mlpp_cache_drop_entry(struct ml_pp_priv *priv, int input_index, int slot)
{
	struct mlpp_dmabuf_cache_entry *e = &priv->dmabuf_cache[input_index][slot];

	if (!e->valid)
		return;

	/* The live mapping published in priv->dmabuf_* may be THIS entry. Never
	 * tear down a mapping the engine is still programmed with -- clearing the
	 * published pointers is the caller's job (ml_pp_cleanup_dmabuf_locked),
	 * and it runs before any eviction can pick this slot. */
	if (e->attach && e->sgt)
		dma_buf_unmap_attachment_unlocked(e->attach, e->sgt, DMA_BIDIRECTIONAL);
	if (e->attach)
		dma_buf_detach(e->dmabuf, e->attach);
	if (e->dmabuf)
		dma_buf_put(e->dmabuf);

	memset(e, 0, sizeof(*e));
}

/* Release every cached import for one input. Called from device release() and
 * module teardown. */
void mlpp_cache_flush(struct ml_pp_priv *priv, int input_index)
{
	int i;

	if (!priv || input_index < 0 || input_index >= 5)
		return;

	lockdep_assert_held(&priv->dmabuf_lock[input_index]);

	for (i = 0; i < MLPP_DMABUF_CACHE_MAX; i++)
		mlpp_cache_drop_entry(priv, input_index, i);
}

/* Look up a dma_buf in the cache. Returns the slot index, or -1 on a miss. */
static int mlpp_cache_find(struct ml_pp_priv *priv, int input_index,
			   struct dma_buf *dmabuf)
{
	int i;

	for (i = 0; i < MLPP_DMABUF_CACHE_MAX; i++) {
		struct mlpp_dmabuf_cache_entry *e = &priv->dmabuf_cache[input_index][i];

		if (e->valid && e->dmabuf == dmabuf)
			return i;
	}
	return -1;
}

/* Pick a slot for a new entry: first free one, else evict least-recently-used.
 * Never returns a slot whose dmabuf is the currently-published one. */
static int mlpp_cache_pick_slot(struct ml_pp_priv *priv, int input_index)
{
	struct dma_buf *live = priv->dmabuf[input_index];
	u64 oldest = U64_MAX;
	int lru = -1;
	int i;

	for (i = 0; i < MLPP_DMABUF_CACHE_MAX; i++) {
		if (!priv->dmabuf_cache[input_index][i].valid)
			return i;
	}

	for (i = 0; i < MLPP_DMABUF_CACHE_MAX; i++) {
		struct mlpp_dmabuf_cache_entry *e = &priv->dmabuf_cache[input_index][i];

		/* Do not evict the entry that is currently programmed into the
		 * engine, however old it looks. */
		if (e->dmabuf == live)
			continue;
		if (e->last_used < oldest) {
			oldest = e->last_used;
			lru = i;
		}
	}

	if (lru < 0)
		return -1;

	mlpp_cache_drop_entry(priv, input_index, lru);
	atomic_inc(&priv->dmabuf_cache_evictions[input_index]);
	return lru;
}

int ml_pp_import_dmabuf(struct ml_pp_priv *priv, int dmabuf_fd, int input_index, u32 force_size)
{
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	size_t eff_size;
	unsigned long flags;
	int cache_slot;
	int ret;

	if (!priv || !priv->dev) {
		pr_err("Invalid private data or device\n");
		return -EINVAL;
	}

	if (input_index < 0 || input_index >= 5) {
		pr_err("Invalid input index %d\n", input_index);
		return -EINVAL;
	}

	// Check if device is in a valid state for DMABUF operations
	// Only reject if no devices are open (system shutting down)
	if (atomic_read(&priv->active_opens[input_index]) == 0) {
		pr_err("No active device opens, rejecting DMABUF import for fd %d\n", dmabuf_fd);
		return -ENODEV;
	}
	DEBUG_PRINT("Device is active (%d opens), accepting DMABUF import for fd %d\n",
	           atomic_read(&priv->active_opens[input_index]), dmabuf_fd);

	/* Serialise the whole replace-then-publish sequence. Without this a
	 * concurrent release() or second write() on the same input can tear down
	 * the buffer we are publishing, or double-put the one we are replacing --
	 * which corrupts the exporter's attachment list. */
	mutex_lock(&priv->dmabuf_lock[input_index]);

	/* Unpublish the previous buffer, but do NOT unmap it: the mapping stays
	 * owned by the cache so the next frame that reuses the same dma_buf is a
	 * hit. This is the change that removes the per-frame unmap/remap cycle --
	 * and with it the per-frame window in which the engine could still be
	 * reading an address we have just torn down. */
	ml_pp_unpublish_dmabuf_locked(priv, input_index);

	// Get DMABUF from file descriptor
	dmabuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		pr_err("Failed to get DMABUF from fd %d: %ld\n", dmabuf_fd, PTR_ERR(dmabuf));
		ret = PTR_ERR(dmabuf);
		goto err_unlock;
	}

	/* Clamp the userspace size hint to what the exporter actually allocated.
	 * A hint larger than the real buffer would make the plane arithmetic in
	 * ml_pp_use_dmabuf_as_input() point the hardware past the allocation. */
	eff_size = dmabuf->size;
	if (force_size > 0 && (size_t)force_size < eff_size) {
		DEBUG_PRINT("DMABUF size hint %u shrinks view from %zu\n",
		            force_size, eff_size);
		eff_size = force_size;
	} else if (force_size > (u32)dmabuf->size) {
		pr_warn("DMABUF size hint %u exceeds exporter size %zu, clamping\n",
		        force_size, dmabuf->size);
	}

	DEBUG_PRINT("DMABUF import: fd=%d, exporter size=%zu, effective size=%zu, ops=%px\n",
	           dmabuf_fd, dmabuf->size, eff_size, dmabuf->ops);

	/* --- cache lookup ------------------------------------------------------
	 * Keyed on the dma_buf pointer, never the fd (see the design note in
	 * ml_pre_process.h). On a hit we already hold a reference and a live
	 * mapping, so drop the extra reference dma_buf_get() just took and
	 * republish the cached addresses.
	 */
	cache_slot = mlpp_cache_find(priv, input_index, dmabuf);
	if (cache_slot >= 0) {
		struct mlpp_dmabuf_cache_entry *e =
			&priv->dmabuf_cache[input_index][cache_slot];

		/* A cached entry was validated when it was created. If the caller
		 * now asks for a LARGER view of the same buffer than we cached,
		 * re-import it rather than silently under-mapping. */
		if (eff_size > e->size) {
			DEBUG_PRINT("cache: size grew %zu -> %zu, re-importing\n",
			            e->size, eff_size);
			mlpp_cache_drop_entry(priv, input_index, cache_slot);
		} else {
			e->last_used = ++priv->dmabuf_cache_clock[input_index];

			spin_lock_irqsave(&priv->lock[0], flags);
			priv->dmabuf[input_index]          = e->dmabuf;
			priv->dmabuf_attach[input_index]   = e->attach;
			priv->dmabuf_sgt[input_index]      = e->sgt;
			priv->dmabuf_dma_addr[input_index] = e->dma_addr;
			priv->dmabuf_size[input_index]     = eff_size;
			priv->using_dmabuf[input_index]    = true;
			spin_unlock_irqrestore(&priv->lock[0], flags);

			/* The cache owns the reference; release the one we just took. */
			dma_buf_put(dmabuf);
			atomic_inc(&priv->dmabuf_cache_hits[input_index]);
			DEBUG_PRINT("cache HIT input %d slot %d dma_addr=0x%pad\n",
			            input_index, cache_slot, &e->dma_addr);
			mutex_unlock(&priv->dmabuf_lock[input_index]);
			return 0;
		}
	}
	atomic_inc(&priv->dmabuf_cache_misses[input_index]);

	// Attach to the DMABUF
	attach = dma_buf_attach(dmabuf, priv->dev);
	if (IS_ERR(attach)) {
		pr_err("Failed to attach to DMABUF: %ld\n", PTR_ERR(attach));
		ret = PTR_ERR(attach);
		goto err_put_dmabuf;
	}

	/* Map for DMA. Use the *_unlocked() variants: since v6.2 the plain
	 * dma_buf_map_attachment() asserts the reservation lock is already held,
	 * which importers like us do not hold. */
	sgt = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		pr_err("Failed to map DMABUF attachment: %ld\n", PTR_ERR(sgt));
		ret = PTR_ERR(sgt);
		goto err_detach;
	}

	/* The hardware programs a single base address per plane and the plane
	 * offsets below are computed by arithmetic on it, so a scattered buffer
	 * would make the engine read unrelated physical memory. Refuse it rather
	 * than silently corrupting. */
	if (sgt->nents != 1) {
		pr_err("DMABUF is not physically contiguous (%u sg entries); unsupported\n",
		       sgt->nents);
		ret = -EINVAL;
		goto err_unmap;
	}

	dma_addr = sg_dma_address(sgt->sgl);
	if (!dma_addr) {
		pr_err("Failed to get DMA address from scatter-gather table\n");
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* The mapped extent must actually cover the payload we intend to read. */
	if (sg_dma_len(sgt->sgl) < eff_size) {
		pr_err("DMABUF mapping too small: sg_dma_len=%u < effective size %zu\n",
		       sg_dma_len(sgt->sgl), eff_size);
		ret = -EINVAL;
		goto err_unmap;
	}

	DEBUG_PRINT("DMABUF mapping successful: sgt->nents=%u, sg_dma_len=%u, dma_addr=0x%pad\n",
	           sgt->nents, sg_dma_len(sgt->sgl), &dma_addr);

	/* --- install in the cache ---------------------------------------------
	 * The cache takes ownership of the reference, the attachment and the
	 * mapping. They are released only on eviction or flush, NOT per frame.
	 *
	 * If no slot is available (every entry is the live buffer -- only
	 * possible with a pathologically small cache) fall back to the old
	 * behaviour: publish it, and it will be torn down on the next import.
	 */
	/* An eviction on a steady-state pipeline means the producer is cycling
	 * through MORE distinct buffers than the cache can hold. That is the LRU
	 * worst case: hit rate collapses to zero AND we evict (unmap/detach/put)
	 * every single frame -- strictly worse than no cache at all. Say so
	 * loudly; the fix is to raise MLPP_DMABUF_CACHE_MAX above the producer's
	 * pool size (decoder default is 8). */
	if (atomic_read(&priv->dmabuf_cache_evictions[input_index]) ==
	    MLPP_DMABUF_CACHE_MAX) {
		pr_warn_ratelimited("mlpp: input %d dmabuf cache thrashing (pool > %d); raise MLPP_DMABUF_CACHE_MAX\n",
		                    input_index, MLPP_DMABUF_CACHE_MAX);
	}

	cache_slot = mlpp_cache_pick_slot(priv, input_index);
	if (cache_slot >= 0) {
		struct mlpp_dmabuf_cache_entry *e =
			&priv->dmabuf_cache[input_index][cache_slot];

		e->dmabuf    = dmabuf;
		e->attach    = attach;
		e->sgt       = sgt;
		e->dma_addr  = dma_addr;
		e->size      = eff_size;
		e->last_used = ++priv->dmabuf_cache_clock[input_index];
		e->valid     = true;
		DEBUG_PRINT("cache FILL input %d slot %d dma_addr=0x%pad\n",
		            input_index, cache_slot, &dma_addr);
	} else {
		pr_warn_ratelimited("mlpp: input %d dmabuf cache full, uncached import\n",
		                    input_index);
	}

	// Store the DMABUF information
	spin_lock_irqsave(&priv->lock[0], flags);
	priv->dmabuf[input_index] = dmabuf;
	priv->dmabuf_attach[input_index] = attach;
	priv->dmabuf_sgt[input_index] = sgt;
	priv->dmabuf_dma_addr[input_index] = dma_addr;
	priv->dmabuf_size[input_index] = eff_size;
	priv->using_dmabuf[input_index] = true;
	spin_unlock_irqrestore(&priv->lock[0], flags);

	DEBUG_PRINT("Successfully imported DMABUF fd %d, DMA addr: 0x%pad, size: %zu\n",
	        dmabuf_fd, &dma_addr, eff_size);

	mutex_unlock(&priv->dmabuf_lock[input_index]);
	return 0;

err_unmap:
	dma_buf_unmap_attachment_unlocked(attach, sgt, DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(dmabuf, attach);
err_put_dmabuf:
	dma_buf_put(dmabuf);
err_unlock:
	/* Leave the input in a consistent "not using dmabuf" state so release()
	 * and the pre-kick validation cannot act on a half-built import. */
	priv->using_dmabuf[input_index] = false;
	mutex_unlock(&priv->dmabuf_lock[input_index]);
	return ret;
}

/* Fully release the imported DMABUF for an input: unmap, detach, drop our ref.
 *
 * Caller MUST hold priv->dmabuf_lock[input_index]. May sleep.
 *
 * There is exactly one teardown path. The previous code had a "reference-only"
 * mode that cleared the pointers without calling unmap/detach/put, on the theory
 * that this avoided a use-after-free when the exporting process died. That
 * reasoning was inverted: holding a dma_buf reference is precisely what keeps
 * the exporter's object alive, so skipping the put could never prevent a UAF --
 * it only guaranteed that our reference and our attachment leaked. The leaked
 * attachment stayed linked on the exporter's dmabuf->attachments list, which is
 * a plausible route to list corruption in the exporter's own teardown.
 */
/* Clear the PUBLISHED pointers only: the engine stops being told about this
 * buffer, but its attachment/mapping stay alive in the cache.
 *
 * This is what runs per frame now. The expensive and dangerous part -- unmap,
 * detach, put -- happens only on eviction, device release or module teardown.
 *
 * Caller MUST hold priv->dmabuf_lock[input_index].
 */
static void ml_pp_unpublish_dmabuf_locked(struct ml_pp_priv *priv, int input_index)
{
	unsigned long flags;

	if (!priv)
		return;

	lockdep_assert_held(&priv->dmabuf_lock[input_index]);

	spin_lock_irqsave(&priv->lock[0], flags);
	priv->using_dmabuf[input_index] = false;
	priv->dmabuf[input_index] = NULL;
	priv->dmabuf_attach[input_index] = NULL;
	priv->dmabuf_sgt[input_index] = NULL;
	priv->dmabuf_dma_addr[input_index] = 0;
	priv->dmabuf_size[input_index] = 0;
	priv->dmabuf_plane1_addr[input_index] = 0;
	priv->dmabuf_plane2_addr[input_index] = 0;
	priv->dmabuf_plane3_addr[input_index] = 0;
	spin_unlock_irqrestore(&priv->lock[0], flags);
}

static void ml_pp_cleanup_dmabuf_locked(struct ml_pp_priv *priv, int input_index)
{
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	unsigned long flags;

	if (!priv)
		return;

	lockdep_assert_held(&priv->dmabuf_lock[input_index]);

	/* Take a consistent snapshot and clear the published state under the
	 * spinlock so the ISR-adjacent readers (ml_pp_use_dmabuf_as_input, the
	 * frame-program path) never observe a half-torn-down buffer. The actual
	 * unmap/detach/put happen outside the spinlock because they can sleep. */
	spin_lock_irqsave(&priv->lock[0], flags);

	dmabuf = priv->dmabuf[input_index];
	attach = priv->dmabuf_attach[input_index];
	sgt    = priv->dmabuf_sgt[input_index];

	priv->using_dmabuf[input_index] = false;
	priv->dmabuf[input_index] = NULL;
	priv->dmabuf_attach[input_index] = NULL;
	priv->dmabuf_sgt[input_index] = NULL;
	priv->dmabuf_dma_addr[input_index] = 0;
	priv->dmabuf_size[input_index] = 0;
	priv->dmabuf_plane1_addr[input_index] = 0;
	priv->dmabuf_plane2_addr[input_index] = 0;
	priv->dmabuf_plane3_addr[input_index] = 0;

	spin_unlock_irqrestore(&priv->lock[0], flags);

	/* Every attachment/mapping/reference is owned by the cache now, including
	 * the one that was published above. Flushing the cache is therefore the
	 * complete teardown -- and it must not be done per frame, which is why
	 * the import path calls ml_pp_unpublish_dmabuf_locked() instead.
	 *
	 * The snapshot taken above is deliberately not unmapped here: doing so
	 * would double-free, because the same attach/sgt is still referenced by a
	 * cache entry that mlpp_cache_flush() is about to drop.
	 */
	(void)dmabuf;
	(void)attach;
	(void)sgt;

	mlpp_cache_flush(priv, input_index);

	DEBUG_PRINT("DMABUF cleanup completed (cache flushed)\n");
}

/* Public teardown: same as above but takes the lock itself. */
void ml_pp_cleanup_dmabuf(struct ml_pp_priv *priv, int input_index)
{
	if (!priv || input_index < 0 || input_index >= 5)
		return;

	mutex_lock(&priv->dmabuf_lock[input_index]);
	ml_pp_cleanup_dmabuf_locked(priv, input_index);
	mutex_unlock(&priv->dmabuf_lock[input_index]);
}

// TODO handle multiple buffers for SVC
// Helper function to update framebuffer read register with DMABUF address
int ml_pp_use_dmabuf_as_input(struct ml_pp_priv *priv, int input_index)
{
	unsigned long flags;
	bool valid_dmabuf;
	dma_addr_t base_addr = 0;
	dma_addr_t plane1_addr, plane2_addr, plane3_addr;
	size_t dmabuf_size = 0;
	struct ml_pp_input_params *input_params;

	if (!priv || input_index < 0 || input_index >= 5)
		return -EINVAL;

	/* Snapshot EVERYTHING we need under the lock. Re-reading priv->dmabuf_*
	 * after dropping it would race ml_pp_cleanup_dmabuf_locked(), which NULLs
	 * exactly these fields under the same lock -- the plane arithmetic could
	 * then be done against a zeroed base and program the engine to DMA from
	 * physical address 0. */
	spin_lock_irqsave(&priv->lock[0], flags);
	valid_dmabuf = priv->using_dmabuf[input_index] && priv->dmabuf_dma_addr[input_index] &&
	               priv->dmabuf[input_index] && priv->dmabuf_attach[input_index] && priv->dmabuf_sgt[input_index];
	if (valid_dmabuf) {
		base_addr = priv->dmabuf_dma_addr[input_index];
		/* Our clamped view of the buffer, not the exporter's dma_buf->size. */
		dmabuf_size = priv->dmabuf_size[input_index];
	}
	spin_unlock_irqrestore(&priv->lock[0], flags);

	if (!valid_dmabuf) {
		pr_err("No valid DMABUF to use as input\n");
		return -EINVAL;
	}

	// Calculate plane addresses based on input format
	input_params = &mlpp_input_params[input_index];

	plane1_addr = base_addr;

	// Format-specific plane address calculation
	switch (input_params->color_in) {
		case MLPP_COLOR_UYVY_420: // NV12 format
			// Y plane: width * height
			// UV plane: width * height / 2 (subsampled)
			plane2_addr = base_addr + (dmabuf_size / 3 * 2); //(width * height);
			plane3_addr = plane2_addr; // NV12 has only 2 planes (Y and interleaved UV)
			DEBUG_PRINT("DMABUF YUV420/NV12 format detected: Y=%pad, UV=%pad\n",
			           &plane1_addr, &plane2_addr);
			break;

		case MLPP_COLOR_YUV888M: // YUV444 format
		default:
			// Y, U, V planes each: width * height
			plane2_addr = base_addr + (dmabuf_size / 3);//width * height);
			plane3_addr = base_addr + (dmabuf_size * 2 / 3);//(width * height * 2);
			DEBUG_PRINT("DMABUF YUV444 format: Y=%pad, U=%pad, V=%pad\n",
			           &plane1_addr, &plane2_addr, &plane3_addr);
			break;
	}

	/* Publish the plane addresses under the same lock, and only if the buffer
	 * is still the one we snapshotted -- a concurrent close() may have torn it
	 * down while we were computing. */
	spin_lock_irqsave(&priv->lock[0], flags);
	if (!priv->using_dmabuf[input_index] ||
	    priv->dmabuf_dma_addr[input_index] != base_addr) {
		spin_unlock_irqrestore(&priv->lock[0], flags);
		pr_err("DMABUF for input %d changed while computing planes\n", input_index);
		return -EAGAIN;
	}
	priv->dmabuf_plane1_addr[input_index] = plane1_addr;
	priv->dmabuf_plane2_addr[input_index] = plane2_addr;
	priv->dmabuf_plane3_addr[input_index] = plane3_addr;
	spin_unlock_irqrestore(&priv->lock[0], flags);

	DEBUG_PRINT("DMABUF plane addresses: P1=0x%pad, P2=0x%pad, P3=0x%pad, size=%zu, format=%d\n",
	           &plane1_addr, &plane2_addr, &plane3_addr, dmabuf_size, input_params->color_in);

	wmb();

	return 0;
}

// DMABUF operations for exporting CMA buffers
struct ml_pp_dmabuf_export_data {
	struct ml_pp_priv *priv;
	int device_index;
	int slot;          // output ring slot captured at export time (pinned)
	size_t size;       // exported payload size for this slot
};

/* Usable payload of one ring slot: the allocation minus the worst-case
 * 64-byte alignment shift applied in the allocator. */
size_t mlpp_slot_payload(int dev)
{
	size_t alloc;

	if (!global_priv || dev < 0 || dev >= 20)
		return 0;

	alloc = (size_t)global_priv->cma_alloc_size[dev] + MLPP_SLOT_SLACK;
	if (alloc <= (MLPP_SLOT_ALIGN - 1))
		return 0;
	return alloc - (MLPP_SLOT_ALIGN - 1);
}

/* Unpin an output ring slot handed to userspace as an exported DMABUF. The
 * matching pin is taken atomically with the rd_idx advance inside
 * mlpp_output_claim_for_export().
 *
 * While pinned, the CMA resize and remove paths must not FREE the slot. The
 * producer deliberately still recycles it -- see the note in
 * mlpp_output_reserve(). */
static void mlpp_slot_unpin(struct ml_pp_priv *priv, int dev, int slot)
{
	atomic_dec(&priv->slot_pinned[dev][slot]);
}

bool mlpp_dev_has_pinned_slots(int dev)
{
	int s;

	if (!global_priv || dev < 0 || dev >= 20)
		return false;

	for (s = 0; s < MLPP_MAX_NBUF; s++)
		if (atomic_read(&global_priv->slot_pinned[dev][s]) > 0)
			return true;
	return false;
}

static struct sg_table *ml_pp_dmabuf_map(struct dma_buf_attachment *attachment,
                                         enum dma_data_direction direction)
{
	struct ml_pp_dmabuf_export_data *export_data = attachment->dmabuf->priv;
	struct ml_pp_priv *priv = export_data->priv;
	int device_index = export_data->device_index;
	struct sg_table *sgt;
	int ret;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	/* Build the sg entry with dma_get_sgtable() rather than
	 * virt_to_page(cpu_addr). dma_alloc_coherent() does not guarantee a
	 * linear-map address -- on arm64 with a non-coherent device it returns a
	 * remapped (vmalloc/ioremap) address for which virt_to_page() yields a
	 * bogus struct page. dma_get_sgtable() asks the DMA API for the correct
	 * page(s) and preserves any intra-page offset. */
	ret = dma_get_sgtable(priv->dev, sgt,
	                      priv->cpu_addr_fb[device_index][export_data->slot],
	                      priv->dma_handle_fb[device_index][export_data->slot],
	                      export_data->size);
	if (ret) {
		pr_err("dma_get_sgtable failed for dev %d slot %d: %d\n",
		       device_index, export_data->slot, ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	/* Map into the importing device's address space so an importer behind an
	 * IOMMU gets a valid address instead of a raw physical one. */
	ret = dma_map_sgtable(attachment->dev, sgt, direction, 0);
	if (ret) {
		pr_err("dma_map_sgtable failed for dev %d slot %d: %d\n",
		       device_index, export_data->slot, ret);
		sg_free_table(sgt);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

static void ml_pp_dmabuf_unmap(struct dma_buf_attachment *attachment,
                              struct sg_table *sgt,
                              enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, sgt, direction, 0);
	sg_free_table(sgt);
	kfree(sgt);
}

static void ml_pp_dmabuf_release(struct dma_buf *dmabuf)
{
	struct ml_pp_dmabuf_export_data *export_data = dmabuf->priv;
	struct ml_pp_priv *priv = export_data->priv;

	DEBUG_PRINT("DMABUF released for output buffer device %d slot %d\n",
	            export_data->device_index, export_data->slot);

	/* Userspace has dropped its last reference, so the CMA resize and remove
	 * paths are free to reclaim this slot again.
	 *
	 * This drops only the *pin*. rd_idx was already advanced at export time so
	 * later reads see the next frame instead of stalling behind an outstanding
	 * export, and the producer never honoured the pin in the first place -- it
	 * may already have overwritten this slot. */
	mlpp_slot_unpin(priv, export_data->device_index, export_data->slot);
	atomic_inc(&priv->stat_export_released[export_data->device_index]);

	// The CMA buffer itself is managed by the main driver, so not freed here.
	kfree(export_data);
}

static int ml_pp_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ml_pp_dmabuf_export_data *export_data = dmabuf->priv;
	struct ml_pp_priv *priv = export_data->priv;
	int device_index = export_data->device_index;
	unsigned long len = vma->vm_end - vma->vm_start;

	DEBUG_PRINT("ml_pp_dmabuf_mmap\n");

	/* Never map beyond the slot we exported. dma_buf_mmap_internal() already
	 * checks against dmabuf->size, but check explicitly against the size we
	 * recorded so the two can never drift. */
	if (len > export_data->size) {
		pr_err("mmap length %lu exceeds exported size %zu\n",
		       len, export_data->size);
		return -EINVAL;
	}

	// Map the CMA buffer to userspace - use the ring slot captured at export.
	return dma_mmap_coherent(priv->dev, vma,
	                        priv->cpu_addr_fb[device_index][export_data->slot],
	                        priv->dma_handle_fb[device_index][export_data->slot],
	                        len);
}

static const struct dma_buf_ops ml_pp_dmabuf_ops = {
	.map_dma_buf = ml_pp_dmabuf_map,
	.unmap_dma_buf = ml_pp_dmabuf_unmap,
	.release = ml_pp_dmabuf_release,
	.mmap = ml_pp_dmabuf_mmap,
};

/* Exported DMABUFs currently alive for a device: every successful export
 * increments stat_exports, and ml_pp_dmabuf_release() increments
 * stat_export_released when userspace drops the last reference. The difference
 * is what userspace is still holding.
 *
 * MLPP_OUT_NBUF slots exist per device, so a well-behaved consumer settles at
 * a small number. A value that climbs without bound means userspace is not
 * close()ing the fds it is handed -- which is exactly how a process walks into
 * its RLIMIT_NOFILE and every later export fails with -EMFILE. */
int mlpp_dev_live_exports(int dev)
{
	int live;

	if (!global_priv || dev < 0 || dev >= 20)
		return 0;

	live = atomic_read(&global_priv->stat_exports[dev]) -
	       atomic_read(&global_priv->stat_export_released[dev]);
	return (live < 0) ? 0 : live;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *mlpp_debugfs_dir;

static int mlpp_stats_show(struct seq_file *m, void *v)
{
	/* Snapshot the global once: ml_pp_remove() clears it, and re-reading it
	 * per field would let it go NULL mid-loop. debugfs_remove_recursive() in
	 * mlpp_debugfs_exit() runs before platform_driver_unregister() and waits
	 * for any reader already inside this function, so a pointer captured here
	 * stays valid for the duration of the call. */
	struct ml_pp_priv *priv = READ_ONCE(global_priv);
	int dev;

	(void)v;

	if (!priv) {
		seq_puts(m, "driver not probed\n");
		return 0;
	}

	/* DMABUF import cache. hits should dominate: the producer recycles a
	 * small fixed pool, so after warm-up nearly every frame should be a hit.
	 * A miss rate that stays high means the producer is NOT recycling and
	 * the cache is not helping -- worth knowing before blaming the engine. */
	for (dev = 0; dev < 5; dev++) {
		int live = 0;
		int i;

		for (i = 0; i < MLPP_DMABUF_CACHE_MAX; i++)
			if (priv->dmabuf_cache[dev][i].valid)
				live++;

		seq_printf(m,
			   "dmabuf_cache%d hits=%d misses=%d evict=%d cached=%d/%d\n",
			   dev,
			   atomic_read(&priv->dmabuf_cache_hits[dev]),
			   atomic_read(&priv->dmabuf_cache_misses[dev]),
			   atomic_read(&priv->dmabuf_cache_evictions[dev]),
			   live, MLPP_DMABUF_CACHE_MAX);
	}

	/* Per-engine IRQ health first: after a stall this is the line that says
	 * whether the IP stopped asserting (entries frozen) or whether we
	 * mishandled an assert (entries advancing but frames not completing). */
	for (dev = 0; dev < MLPP_NUM_ENGINES; dev++) {
		struct mlpp_engine const *e = &priv->engine[dev];

		seq_printf(m,
			   "engine%d base_writes=%d base_mismatch=%d%s\n",
			   dev,
			   atomic_read(&e->base_verify_writes),
			   atomic_read(&e->base_verify_fail),
			   atomic_read(&e->base_verify_fail) ?
				"   *** IN_DMA_BASE READ-BACK FAILED ***" :
				(verify_base ? "" : "   (verify_base=0, not checked)"));

		seq_printf(m,
			   "engine%d rd_irq=%d wr_irq=%d rd_none=%d wr_none=%d drain_max=%d busy=%d done_mask=0x%02x\n",
			   dev,
			   atomic_read(&e->irq_rd_entries),
			   atomic_read(&e->irq_wr_entries),
			   atomic_read(&e->irq_rd_none),
			   atomic_read(&e->irq_wr_none),
			   atomic_read(&e->irq_drain_max),
			   e->busy ? 1 : 0,
			   e->done_mask);
	}

	seq_puts(m, "dev  done      drops     timeouts  exports   released  live  fail  wr rd\n");
	for (dev = MLPP_FIRST_OUT_DEV; dev < 20; dev++) {
		int live = atomic_read(&priv->stat_exports[dev]) -
			   atomic_read(&priv->stat_export_released[dev]);

		seq_printf(m,
			   "%-4d %-9d %-9d %-9d %-9d %-9d %-5d %-5d %2d %2d\n",
			   dev,
			   atomic_read(&priv->stat_frames_done[dev]),
			   atomic_read(&priv->stat_drops[dev]),
			   atomic_read(&priv->stat_wait_timeouts[dev]),
			   atomic_read(&priv->stat_exports[dev]),
			   atomic_read(&priv->stat_export_released[dev]),
			   (live < 0) ? 0 : live,
			   atomic_read(&priv->stat_export_fail[dev]),
			   priv->wr_idx[dev],
			   priv->rd_idx[dev]);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mlpp_stats);

void mlpp_debugfs_init(void)
{
	mlpp_debugfs_dir = debugfs_create_dir("mlpp", NULL);
	if (IS_ERR_OR_NULL(mlpp_debugfs_dir)) {
		mlpp_debugfs_dir = NULL;
		return;
	}
	debugfs_create_file("stats", 0444, mlpp_debugfs_dir, NULL,
			    &mlpp_stats_fops);
}

void mlpp_debugfs_exit(void)
{
	debugfs_remove_recursive(mlpp_debugfs_dir);
	mlpp_debugfs_dir = NULL;
}
#else
void mlpp_debugfs_init(void) { }
void mlpp_debugfs_exit(void) { }
#endif /* CONFIG_DEBUG_FS */

// Export output CMA buffer as DMABUF for output devices
int ml_pp_export_dmabuf(int device_index, char __user *buf)
{
	struct dma_buf *dmabuf;
	struct dma_buf_export_info exp_info;
	struct ml_pp_dmabuf_export_data *export_data;
	int image_out_size = 0;
	int slot;
	int fd;
	int ret;

	if (device_index < MLPP_FIRST_OUT_DEV || device_index >= 20) {
		pr_err("Invalid output device index %d\n", device_index);
		return -EINVAL;
	}

	if (!global_priv || !global_priv->cpu_addr_fb[device_index][0]) {
		pr_err("No CMA buffer allocated for device %d\n", device_index);
		return -ENOMEM;
	}

	if (!mlpp_params_lookup[device_index]) {
		pr_err("Device %d has no output parameters\n", device_index);
		return -EINVAL;
	}

	/* Backpressure against a leaking consumer. The kernel cannot revoke a
	 * dma_buf fd once it is installed in userspace, so the only way to stop
	 * an unbounded pile-up (thousands of alive exports on a 4-deep ring) is
	 * to refuse to create new ones until the consumer closes what it holds.
	 * A well-behaved consumer settles at a couple of exports; the cap is set
	 * well above that so normal double/triple-buffering is never rejected.
	 * Turning a silent leak into a bounded -EMFILE also surfaces the bug to
	 * userspace immediately instead of at RLIMIT_NOFILE thousands later. */
	if (mlpp_dev_live_exports(device_index) >= MLPP_MAX_LIVE_EXPORTS) {
		pr_err_ratelimited("mlpp: device %d refusing DMABUF export: %d already alive (cap %d, ring %d) -- consumer is not closing fds\n",
				   device_index, mlpp_dev_live_exports(device_index),
				   MLPP_MAX_LIVE_EXPORTS, MLPP_OUT_NBUF);
		atomic_inc(&global_priv->stat_export_fail[device_index]);
		return -EMFILE;
	}

	// Allocate export data structure
	export_data = kmalloc(sizeof(*export_data), GFP_KERNEL);
	if (!export_data) {
		pr_err("Failed to allocate export data\n");
		return -ENOMEM;
	}
	export_data->priv = global_priv;
	export_data->device_index = device_index;

	// Wait for the current ring slot to be filled.
	ret = mlpp_output_wait_ready(device_index);
	if (ret) {
		/* Only -ETIMEDOUT is a real timeout; -ERESTARTSYS is a signal and
		 * is already filtered out here. Rate-limited so a wedged engine
		 * cannot flood the log. */
		if (ret != -ERESTARTSYS)
			pr_warn_ratelimited("DMABUF: Timeout waiting for output ring slot for device %d\n",
			                    device_index);
		output_byte_count[device_index] = 0;
		reinit_completion(&global_priv->read_complete[device_index]);
		global_priv->read_ready[device_index] = false;
		atomic_inc(&global_priv->stat_export_fail[device_index]);
		kfree(export_data);
		return ret;   /* preserve -ERESTARTSYS so the read can be restarted */
	}

	// Calculate output image size
	if (mlpp_params_lookup[device_index]->color_out == MLPP_COLOR_OUT_NV12) // NV12: 1.5 bytes per pixel
		image_out_size = mlpp_params_lookup[device_index]->width_out *
		                mlpp_params_lookup[device_index]->height_out * 3 / 2;
	else if (mlpp_params_lookup[device_index]->color_out < 2) // 3 bytes per pixel
		image_out_size = mlpp_params_lookup[device_index]->width_out * 
		                mlpp_params_lookup[device_index]->height_out * 3;
	else if (mlpp_params_lookup[device_index]->color_out == 3) // 3 bytes per pixel
		image_out_size = mlpp_params_lookup[device_index]->width_out * 
		                mlpp_params_lookup[device_index]->height_out;
	else // 4 bytes per pixel
		image_out_size = mlpp_params_lookup[device_index]->width_out * 
		                mlpp_params_lookup[device_index]->height_out * 4;
	
	/* The exported view must not exceed the usable payload of a slot. */
	if (image_out_size <= 0 ||
	    (size_t)image_out_size > mlpp_slot_payload(device_index)) {
		pr_err("Computed output size %d invalid for device %d (usable %zu)\n",
		       image_out_size, device_index, mlpp_slot_payload(device_index));
		kfree(export_data);
		return -EINVAL;
	}
	export_data->size = image_out_size;

	/* Claim the frame at the head of the ring: pin the slot AND advance
	 * rd_idx as one atomic step. Capturing rd_idx and consuming it in two
	 * separate unlocked operations would let the producer's drop-oldest move
	 * rd_idx in between, so we could pin one slot and consume a different
	 * one -- desyncing the ring and exporting a frame the engine is actively
	 * overwriting. The pin is dropped in ml_pp_dmabuf_release().
	 *
	 * Taken under buf_sem (read) so the claim is ordered against the CMA
	 * realloc in ml_pp_write(), which holds buf_sem for write while it tests
	 * for pinned slots and frees them. Deliberately NOT the engine lock: that
	 * would quiesce the hardware on every frame export. */
	down_read(&global_priv->buf_sem[device_index]);
	slot = mlpp_output_claim_for_export(device_index);
	up_read(&global_priv->buf_sem[device_index]);

	/* The producer's drop-oldest can move rd_idx between the wait above and
	 * the claim, so the head slot may have just been recycled even though a
	 * frame was ready a moment ago. Wait once more rather than handing
	 * userspace a spurious -EAGAIN for what is a transient race. The retry
	 * only needs to absorb a race that happened microseconds ago, so it waits
	 * one frame period, not another full 2s. */
	if (slot < 0) {
		ret = mlpp_output_wait_ready_timeout(device_index, 50);
		if (ret == -ERESTARTSYS) {
			/* Signalled: let userspace restart the read, same as the
			 * first wait does, rather than reporting -EAGAIN. */
			kfree(export_data);
			return ret;
		}
		if (ret == 0) {
			down_read(&global_priv->buf_sem[device_index]);
			slot = mlpp_output_claim_for_export(device_index);
			up_read(&global_priv->buf_sem[device_index]);
		}
	}

	if (slot < 0) {
		pr_warn("DMABUF: no ready frame to claim for device %d\n", device_index);
		reinit_completion(&global_priv->read_complete[device_index]);
		global_priv->read_ready[device_index] = false;
		atomic_inc(&global_priv->stat_export_fail[device_index]);
		kfree(export_data);
		return -EAGAIN;
	}
	export_data->slot = slot;

	// Set up DMABUF export info
	memset(&exp_info, 0, sizeof(exp_info));
	exp_info.exp_name = KBUILD_MODNAME;
	/* Pin this module while the dma_buf lives. Without an owner,
	 * try_module_get(NULL) succeeds silently and rmmod can unload us while
	 * exported buffers are still alive -- their later release/map/mmap calls
	 * would then jump into unmapped module text. */
	exp_info.owner = THIS_MODULE;
	exp_info.ops = &ml_pp_dmabuf_ops;
	exp_info.priv = export_data;
	exp_info.size = image_out_size;
	exp_info.flags = O_RDONLY; // Output buffers are read-only for userspace

	// Export the CMA buffer as DMABUF
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		pr_err("Failed to export DMABUF for device %d: %ld\n",
		       device_index, PTR_ERR(dmabuf));
		/* No .release will ever run for this buffer, so unpin by hand. */
		mlpp_slot_unpin(global_priv, device_index, slot);
		kfree(export_data);
		return PTR_ERR(dmabuf);
	}

	/* Reserve an fd, hand the buffer to userspace only once we know the fd
	 * number can be delivered. dma_buf_fd() would fd_install() immediately,
	 * leaving an un-revocable installed fd if the subsequent copy_to_user()
	 * failed -- the caller could never learn the number to close it. */
	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		/* -EMFILE here means the CONSUMER is leaking: it keeps asking for
		 * exported frames without close()ing the fds it already holds.
		 * Report the live count so the leak is attributable from dmesg
		 * alone, and rate-limit so a wedged consumer cannot flood the log. */
		pr_err_ratelimited("mlpp: device %d: cannot allocate fd (%d); %d exported DMABUF(s) still held by userspace -- consumer is leaking fds\n",
				   device_index, fd, mlpp_dev_live_exports(device_index));
		atomic_inc(&global_priv->stat_export_fail[device_index]);
		dma_buf_put(dmabuf);   /* drops the export ref; release() unpins */
		return fd;
	}

	ret = copy_to_user(buf, &fd, sizeof(fd));
	if (ret) {
		pr_err("Failed to copy DMABUF fd to user\n");
		put_unused_fd(fd);     /* not installed yet, so this is correct */
		dma_buf_put(dmabuf);
		atomic_inc(&global_priv->stat_export_fail[device_index]);
		return -EFAULT;
	}

	/* Point of no return: the fd is now visible to userspace and owns the
	 * dma_buf reference created by dma_buf_export(). A racing close() can run
	 * ml_pp_dmabuf_release() -- and kfree(export_data) -- the instant this
	 * returns, so nothing below may dereference export_data or dmabuf. Use
	 * the locally captured `slot` instead. */
	fd_install(fd, dmabuf->file);
	atomic_inc(&global_priv->stat_exports[device_index]);

	/* Only MLPP_OUT_NBUF slots exist per device, so a consumer that is
	 * closing its fds cannot legitimately hold many more than that. Warn
	 * (rate-limited) as soon as it does, long before RLIMIT_NOFILE is hit --
	 * this is the early warning for a userspace dmabuf fd leak. */
	if (mlpp_dev_live_exports(device_index) > (4 * MLPP_OUT_NBUF))
		pr_warn_ratelimited("mlpp: device %d has %d exported DMABUFs alive (ring is only %d deep) -- userspace is probably not closing them\n",
				    device_index, mlpp_dev_live_exports(device_index),
				    MLPP_OUT_NBUF);

	DEBUG_PRINT("Exported DMABUF fd %d for device %d slot %d, size %d bytes\n",
	           fd, device_index, slot, image_out_size);

	PERF_PRINT("PIPE OUT-DMABUF, READ %d, NS=%lld\n", device_index, ktime_get_real_ns());

	/* rd_idx was already advanced by mlpp_output_claim_for_export(); the slot
	 * stays pinned until userspace drops the fd. */
	reinit_completion(&global_priv->read_complete[device_index]);
	global_priv->read_ready[device_index] = false;

	/* This frame left via DMABUF, so any partial bulk-read accumulation for
	 * this device is stale -- clear it so a later bulk read() starts at
	 * offset 0 instead of mid-frame. */
	output_byte_count[device_index] = 0;

	return sizeof(fd);
}

// ================================================================
// Platform Driver Functions
// ================================================================
static int ml_pp_probe(struct platform_device *pdev)
{
	int ret;
	uintptr_t aligned_cpu_addr, offset;
	struct device_node *np = pdev->dev.of_node;
	struct ml_pp_priv *priv;
	char device_name[32];

	pr_info("ML pre-processing driver probe called\n");
	
	// Check if we have a device tree node
	if (!np) {
		pr_warn("No device tree node found\n");
	} else {
		pr_info("Device tree node found: %s\n", np->name);
	}

	// Use the device from the platform bus, which has proper DMA ops
	char_dev_parent = &pdev->dev;
	
	// init variables
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = char_dev_parent;
	
	// Initialize DMABUF fields
	for( int i =0; i<5; i++) {
		priv->dmabuf[i] = NULL;
		priv->dmabuf_attach[i] = NULL;
		priv->dmabuf_sgt[i] = NULL;
		priv->dmabuf_dma_addr[i] = 0;
		priv->dmabuf_size[i] = 0;
		priv->dmabuf_plane1_addr[i] = 0;
		priv->dmabuf_plane2_addr[i] = 0;
		priv->dmabuf_plane3_addr[i] = 0;
		priv->using_dmabuf[i] = false;
		atomic_set(&priv->active_opens[i], 0);
		mutex_init(&priv->dmabuf_lock[i]);
	}

	// No output ring slot is exported yet.
	for (int i = 0; i < 20; i++) {
		for (int s = 0; s < MLPP_MAX_NBUF; s++)
			atomic_set(&priv->slot_pinned[i][s], 0);
		init_rwsem(&priv->buf_sem[i]);

		/* Diagnostic counters start from a known zero so the deltas
		 * reported through debugfs are meaningful from probe onward. */
		atomic_set(&priv->stat_frames_done[i], 0);
		atomic_set(&priv->stat_drops[i], 0);
		atomic_set(&priv->stat_wait_timeouts[i], 0);
		atomic_set(&priv->stat_exports[i], 0);
		atomic_set(&priv->stat_export_released[i], 0);
		atomic_set(&priv->stat_export_fail[i], 0);
	}
	
	// Initialize the per-engine spinlocks.
	for (int i = 0; i < 3; i++) {
		spin_lock_init(&priv->lock[i]);
	}
	for (int i = 0; i < 20; i++) {
		init_completion(&priv->read_complete[i]);
		priv->read_ready[i] = false;
	}
		

	// 1. Create a dynamic device number.
	ret = alloc_chrdev_region(&ml_pp_dev_num, 0, NUM_DEVICES, DRIVER_NAME);
	if (ret < 0) {
		pr_err("Failed to allocate char dev region\n");
		kfree(priv);
		return ret;
	}
	
	// 2. Allocate and initialize the character devices
	ml_pp_cdevs = kzalloc(NUM_DEVICES * sizeof(struct cdev), GFP_KERNEL);
	if (!ml_pp_cdevs) {
		pr_err("Failed to allocate cdev array\n");
		ret = -ENOMEM;
		goto fail_chrdev;
	}
	
	// Allocate array to store device pointers
	ml_pp_devices = kzalloc(NUM_DEVICES * sizeof(struct device *), GFP_KERNEL);
	if (!ml_pp_devices) {
		pr_err("Failed to allocate device array\n");
		ret = -ENOMEM;
		goto fail_cdevs;
	}
	
	// Initialize and register each character device
	for (int i = 0; i < NUM_DEVICES; i++) {
		cdev_init(&ml_pp_cdevs[i], &ml_pp_fops[i]);
		ml_pp_cdevs[i].owner = THIS_MODULE;
		
		ret = cdev_add(&ml_pp_cdevs[i], MKDEV(MAJOR(ml_pp_dev_num), MINOR(ml_pp_dev_num) + i), 1);
		if (ret < 0) {
			pr_err("Failed to add character device %d\n", i);
			/* Nodes for 0..i-1 already exist and must be destroyed, so
			 * enter the cascade at fail_device_create, not below it. */
			goto fail_device_create;
		}
		
		// 3. Create the device for each minor number
		snprintf(device_name, sizeof(device_name), "%s", mlpp_device_names[i]);
		
		
		ml_pp_devices[i] = device_create(ml_pre_process_class, char_dev_parent, 
			MKDEV(MAJOR(ml_pp_dev_num), MINOR(ml_pp_dev_num) + i),
			NULL, device_name);
			
		if (IS_ERR(ml_pp_devices[i])) {
			pr_err("Failed to create device %d\n", i);
			ret = PTR_ERR(ml_pp_devices[i]);
			goto fail_device_create;
		}
		
		pr_info("Created device node: /dev/%s\n", device_name);
	}
	
	pr_info("ML pre-processing platform driver probed successfully\n");


	// Allocate a FB  for the entire session
	if (dma_set_mask_and_coherent(priv->dev, DMA_BIT_MASK(64))) {
        dev_err(priv->dev, "Failed to set DMA mask to 64-bit\n");
        ret = -ENOMEM;
        /* Char devices and /dev nodes are already live at this point; a bare
         * return would leave them registered against a driver with no priv,
         * so any open()+write() would NULL-deref global_priv. */
        goto fail_priv;
    }

	// Allocate framebuffers: output devices (5-19) get a MLPP_OUT_NBUF-deep
	// ring so the hardware/ISR producer can run ahead of a slow reader; input
	// devices (0-4) keep a 2-deep ping-pong. Slots live at cpu_addr_fb[i][0..].
	for (int i = 0; i < 20; i++) {
		int nbuf = mlpp_dev_nbuf(i);
		/* Invariant relied on by mlpp_slot_payload() and every free site:
		 * a slot is always allocated as cma_alloc_size + MLPP_SLOT_SLACK. */
		size_t buf_size = ONE_MB_BUFFER_SIZE*mlpp_device_cma_sizes[i] + MLPP_SLOT_SLACK;
		int s;

		priv->cma_alloc_size[i] = ONE_MB_BUFFER_SIZE*mlpp_device_cma_sizes[i];

		for (s = 0; s < nbuf; s++) {
			DEBUG_PRINT("Allocating framebuffer (%s) slot %d: dev=%px, size=%zu\n",
				mlpp_device_names[i], s, priv->dev, buf_size);

			priv->cpu_addr_fb_orig[i][s] = dma_alloc_coherent(priv->dev, buf_size,
				&priv->dma_handle_fb_orig[i][s], GFP_KERNEL);
			if (!priv->cpu_addr_fb_orig[i][s]) {
				pr_err("dma_alloc_coherent failed for dev %d slot %d!\n", i, s);
				/* fail_priv walks every device and frees whatever was
				 * allocated, using each device's own cma_alloc_size, so no
				 * manual unwind is needed (and doing one here would leave
				 * non-NULL pointers for it to free a second time). */
				ret = -ENOMEM;
				goto fail_priv;
			}

			// Ensure 64-byte alignment for this slot
			aligned_cpu_addr = (uintptr_t)priv->cpu_addr_fb_orig[i][s];
			offset = (64 - (aligned_cpu_addr & 63)) & 63;
			priv->cpu_addr_fb[i][s] = (void *)(aligned_cpu_addr + offset);
			priv->dma_handle_fb[i][s] = priv->dma_handle_fb_orig[i][s] + offset;

			priv->buffer_written[i][s] = false;
		}

		// Initialise ring / ping-pong indices
		priv->current_write_buffer[i] = 0;  // input ping-pong starts at slot 0
		priv->wr_idx[i] = 0;                 // output ring producer index
		priv->rd_idx[i] = 0;                 // output ring consumer index

		pr_info("Allocated %d-deep framebuffer: dev=%px, size=%zu, slot0 cpu=%px dma=%pad (aligned)\n",
			nbuf, priv->dev, buf_size,
			priv->cpu_addr_fb[i][0], &priv->dma_handle_fb[i][0]);
	}

	// Store the private data with the platform device
	platform_set_drvdata(pdev, priv);
	
	// Make the DMA buffers globally accessible
	global_priv = priv;
	
	// Initialize global state variables after global_priv assignment
	for( int i=0; i<20; i++)
	{
		init_completion(&global_priv->read_complete[i]);
		global_priv->read_ready[i] = false;
	}
    
	// MAP REGISTERS + request IRQs for the mlpp-v2 FVC and SVC engines.
	// Both engines' RD/WR windows are described in the device tree by reg-name
	// ("fvc_rd", "fvc_wr", "svc_rd", "svc_wr"); interrupts by the same names.
	ret = mlpp_hw_map_and_request(pdev, priv);
	if (ret) {
		pr_err("Failed to map mlpp-v2 hardware: %d\n", ret);
		goto fail_priv;
	}

    return 0;

fail_priv:
	/* Release everything hanging off priv: the CMA ring slots (any that were
	 * allocated), then priv itself. global_priv/drvdata may already point at
	 * it, so clear them -- probe failure means ml_pp_remove() is never called
	 * and a stale non-NULL global_priv would be dereferenced by the fops. */
	for (int i = 0; i < 20; i++) {
		int nbuf = mlpp_dev_nbuf(i);
		size_t sz = (size_t)priv->cma_alloc_size[i] + MLPP_SLOT_SLACK;

		for (int s = 0; s < nbuf; s++) {
			if (priv->cpu_addr_fb_orig[i][s]) {
				dma_free_coherent(priv->dev, sz,
					priv->cpu_addr_fb_orig[i][s],
					priv->dma_handle_fb_orig[i][s]);
				priv->cpu_addr_fb_orig[i][s] = NULL;
			}
		}
	}
	global_priv = NULL;
	platform_set_drvdata(pdev, NULL);
	kfree(priv);
	priv = NULL;   /* so the fall-through to fail_chrdev cannot free it twice */

fail_device_create:
	// Clean up any devices that were successfully created
	for (int i = 0; i < NUM_DEVICES; i++) {
		if (!IS_ERR_OR_NULL(ml_pp_devices[i])) {
			device_destroy(ml_pre_process_class, MKDEV(MAJOR(ml_pp_dev_num), MINOR(ml_pp_dev_num) + i));
		}
	}
	
	// Clean up any cdevs that were successfully added
	for (int i = 0; i < NUM_DEVICES; i++) {
		if (ml_pp_cdevs[i].count > 0) { // Check if the cdev was initialized
			cdev_del(&ml_pp_cdevs[i]);
		}
	}
	kfree(ml_pp_devices);
	
fail_cdevs:
	kfree(ml_pp_cdevs);
	
fail_chrdev:
	unregister_chrdev_region(ml_pp_dev_num, NUM_DEVICES);
	/* Reached either directly from the array-allocation failures above (priv
	 * still live and unpublished) or by fall-through from fail_priv, which
	 * NULLs priv after freeing it. kfree(NULL) is a no-op either way. */
	kfree(priv);
	return ret;
}

static void ml_pp_remove(struct platform_device *pdev)
{
	struct ml_pp_priv *priv = platform_get_drvdata(pdev);
	
	if (priv) {
		// Quiesce the mlpp-v2 hardware (mask IRQs, stop DMA). The IRQ lines
		// and register mappings are devm-managed and freed automatically.
		mlpp_hw_release(priv);

		// Clear active opens to prevent new DMABUF operations during removal
		for (int i = 0; i < 5; i++)
			atomic_set(&priv->active_opens[i], 0);

		/* Release every imported input DMABUF. This is a full unmap/detach/put
		 * for all five inputs, not just index 0. Dropping our references here
		 * is correct and necessary: they are references we own, and holding
		 * them past module removal would leak the exporter's buffers forever.
		 * (The old code skipped this "to avoid use-after-free", which had the
		 * ownership backwards.) */
		for (int i = 0; i < 5; i++)
			ml_pp_cleanup_dmabuf(priv, i);

		// Wake up any waiting processes before cleanup
		for( int i =0; i< 20; i++) {
			priv->read_ready[i] = true;
			global_priv->read_ready[i] = true;
			complete_all(&priv->read_complete[i]);
		}

		/* Exported DMABUFs keep a pointer to priv and to the CMA slots. Unbind
		 * can be triggered independently of rmmod (exp_info.owner only blocks
		 * the latter), so if any slot is still pinned, freeing the CMA and priv
		 * here would leave userspace mappings pointing at recycled pages and a
		 * later .release() dereferencing freed memory. Refuse to tear those
		 * parts down; the device nodes are still removed below so no new
		 * exports can be created. */
		for (int i = 0; i < 20; i++) {
			if (mlpp_dev_has_pinned_slots(i)) {
				pr_err("mlpp: device %d still has exported DMABUFs at remove; leaking CMA and priv to avoid use-after-free\n",
				       i);
				goto skip_buffer_free;
			}
		}

		for (int i = 0; i < 20; i++) {
			int nbuf = mlpp_dev_nbuf(i);
			size_t buf_size = (size_t)priv->cma_alloc_size[i] + MLPP_SLOT_SLACK;
			int s;

			/* Exclude any reader parked in copy_to_user() on this device's
			 * slots before freeing them -- same discipline as the resize
			 * path in ml_pp_write(). Unbind is not gated by the module
			 * refcount, so this is reachable. */
			down_write(&priv->buf_sem[i]);

			// Free every ring slot for this device.
			for (s = 0; s < nbuf; s++) {
				pr_info("About to free Framebuffer dev %d slot %d: size=%zu, cpu=%px dma=%pad\n",
					i, s, buf_size, priv->cpu_addr_fb_orig[i][s],
					&priv->dma_handle_fb_orig[i][s]);

				if (priv->cpu_addr_fb_orig[i][s] && priv->dev) {
					dma_free_coherent(priv->dev, buf_size,
						priv->cpu_addr_fb_orig[i][s],
						priv->dma_handle_fb_orig[i][s]);
					pr_info("Framebuffer dev %d slot %d freed successfully\n", i, s);
				} else {
					pr_err("Cannot free Framebuffer dev %d slot %d: cpu=%px, dev=%px\n",
						i, s, priv->cpu_addr_fb_orig[i][s], priv->dev);
				}
				priv->cpu_addr_fb_orig[i][s] = NULL;
				priv->cpu_addr_fb[i][s] = NULL;
			}

			up_write(&priv->buf_sem[i]);
		}

		kfree(priv);
		global_priv = NULL;
	}

skip_buffer_free:

	// Clean up all device nodes
	for (int i = 0; i < NUM_DEVICES; i++) {
		if (!IS_ERR_OR_NULL(ml_pp_devices[i])) {
			device_destroy(ml_pre_process_class, MKDEV(MAJOR(ml_pp_dev_num), MINOR(ml_pp_dev_num) + i));
		}
	}
	
	// Clean up character devices
	for (int i = 0; i < NUM_DEVICES; i++) {
		if (ml_pp_cdevs[i].count > 0) {
			cdev_del(&ml_pp_cdevs[i]);
		}
	}
	
	// Free allocated memory
	kfree(ml_pp_devices);
	kfree(ml_pp_cdevs);
	
	// Unregister the device region
	unregister_chrdev_region(ml_pp_dev_num, NUM_DEVICES);
	pr_info("ml-pre-process platform driver removed\n");

}

// Device Tree match table
static const struct of_device_id ml_pp_of_match[] = {
	{ .compatible = "xlnx,ml-pre-process-v2" },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, ml_pp_of_match);

static struct platform_driver ml_pp_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ml_pp_of_match,
	},
	.probe = ml_pp_probe,
	.remove = ml_pp_remove,
};

static int __init ml_pp_init(void)
{
	int ret;

	// Initialize global timeout value for read operations
	timeout_33ms = msecs_to_jiffies(33);

	// 1. Create the device class before registering the platform driver
	ml_pre_process_class = class_create(DRIVER_NAME);
	if (IS_ERR(ml_pre_process_class)) {
		pr_err("Failed to create class\n");
		return PTR_ERR(ml_pre_process_class);
	}
	
	// 2. Register our platform driver - it will bind to device tree nodes with matching compatible string
	ret = platform_driver_register(&ml_pp_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		goto fail_class;
	}

	mlpp_debugfs_init();

	pr_info("ML pre-processing driver loaded successfully - waiting for device tree match\n");
	return 0;

fail_class:
	class_destroy(ml_pre_process_class);
	return ret;
}

static void __exit ml_pp_exit(void)
{
	mlpp_debugfs_exit();
	platform_driver_unregister(&ml_pp_driver);
	class_destroy(ml_pre_process_class);
	pr_info("ML pre-processing driver unloaded\n");
}


const struct file_operations ml_pp_fops[20] = {
	[0] = { //FFC in
		.owner = THIS_MODULE,
		.open = mlpp_ffc_in_open,
		.write = mlpp_ffc_in_write,
		.read = mlpp_ffc_in_read,
		.release = mlpp_ffc_in_release,
	},
	[1] = { //SVC 0 in
		.owner = THIS_MODULE,
		.open = mlpp_svc_in_open,
		.write = mlpp_svc_in_write,
		.read = mlpp_svc_in_read,
		.release = mlpp_svc_in_release,
	},
	[2] = { //SVC 1 in
		.owner = THIS_MODULE,
		.open = mlpp_svc_in_open,
		.write = mlpp_svc_in_write,
		.read = mlpp_svc_in_read,
		.release = mlpp_svc_in_release,
	},
	[3] = { // SVC 2 in
		.owner = THIS_MODULE,
		.open = mlpp_svc_in_open,
		.write = mlpp_svc_in_write,
		.read = mlpp_svc_in_read,
		.release = mlpp_svc_in_release,
	},
	[4] = { // SVC 3 in
		.owner = THIS_MODULE,
		.open = mlpp_svc_in_open,
		.write = mlpp_svc_in_write,
		.read = mlpp_svc_in_read,
		.release = mlpp_svc_in_release,
	},
	[5] = { // outputs from here on
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[6] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[7] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[8] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[9] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[10] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[11] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[12] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[13] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[14] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[15] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[16] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[17] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[18] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	},
	[19] = {
		.owner = THIS_MODULE,
		.open = ml_pp_open,
		.write = ml_pp_write,
		.read = ml_pp_read,
		.release = ml_pp_release,
	}
};



module_init(ml_pp_init);
module_exit(ml_pp_exit);
