// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: KC, LT
// Maintainer: LT

/*
 * vpss_helpers.c - mlpp-v2 hardware backend.
 *
 * This file drives the NEW mlpp-v2 IP (crop -> bilinear downscale -> CSC ->
 * subtract -> bitshift), replacing the old multi-block VPSS pipeline. The IP
 * exposes two AXI4-Lite windows per engine (RD = input DMA, WR = per-ROI
 * processing/CSC) and completes one whole frame (all ROIs) per GO.
 *
 * Two engines are driven from the same driver:
 *   FVC : 7 output ROIs  -> char devices 5..11
 *   SVC : 2 output ROIs  -> per camera k: dev 12+k (ROI0), dev 16+k (ROI1)
 *
 * The character-device read()/write() interface is unchanged; this file only
 * translates a "full input frame received" event into one hardware frame and
 * marks the corresponding output double-buffers ready for read().
 */

#include "ml_pre_process.h"

/* ---- register access ------------------------------------------------------ */
static inline u32 rdreg(void __iomem *base, u32 off)
{
	return ioread32(base + off);
}
static inline void wrreg(void __iomem *base, u32 off, u32 val)
{
	iowrite32(val, base + off);
#if 0 // Test if AXI writes are taking affect
	u32 readback;

	/* MLPP_ISR is write-1-to-clear: the value read back is the (now cleared)
	 * status, never what we wrote, so skip the verify for it. */
	if (off == MLPP_ISR)
		return;

	readback = ioread32(base + off);
	if (readback != val)
		pr_err("mlpp: wrreg mismatch at off=0x%x: wrote 0x%08x read 0x%08x\n",
		       off, val, readback);
#endif
}

/* ---- input-base write + verifying read-back ------------------------------
 *
 * Programs the 64-bit input DMA base address into the MLPP read engine and
 * immediately reads both halves back, so we can prove what the IP actually
 * latched rather than what we believed we wrote.
 *
 * Why this exists: with no IOMMU on this platform, sg_dma_address() is a raw
 * physical address written straight into MLPP_IN_DMA_BASE_LO/HI. If the value
 * the hardware holds ever differs from the value we wrote -- a dropped or
 * reordered AXI-Lite write, a marginal path, a register that does not latch
 * the upper half -- the engine DMAs from the wrong physical address and the
 * result is a silent wild read. Nothing else in the system would report it.
 *
 * The read-back is ordered after the writes by ioread32()'s implicit barrier,
 * so this genuinely observes the register rather than a posted write buffer.
 *
 * Controlled by the `verify_base` module parameter:
 *   0 = off
 *   1 = print only on MISMATCH (recommended for soak runs)
 *   2 = print every frame (verbose; ~150 lines/s across 5 channels)
 *
 * Returns true if the read-back matched.
 */
static bool mlpp_write_in_base_verify(struct mlpp_engine *e, int src,
				      dma_addr_t in_base)
{
	/* struct mlpp_engine carries no id, so derive it from its position in
	 * the global array: 0 = FVC, 1 = SVC. */
	int const eng = (global_priv && e == &global_priv->engine[MLPP_ENGINE_SVC])
			? MLPP_ENGINE_SVC : MLPP_ENGINE_FVC;
	u32 const want_lo = lower_32_bits(in_base);
	u32 const want_hi = upper_32_bits(in_base);
	u32 got_lo, got_hi;
	u64 got;
	bool match;

	wrreg(e->rd, MLPP_IN_DMA_BASE_LO, want_lo);
	wrreg(e->rd, MLPP_IN_DMA_BASE_HI, want_hi);

	/* Read back what the IP latched. ioread32() cannot be reordered before
	 * the iowrite32()s above, so this is a true post-write observation. */
	got_lo = rdreg(e->rd, MLPP_IN_DMA_BASE_LO);
	got_hi = rdreg(e->rd, MLPP_IN_DMA_BASE_HI);

	got   = ((u64)got_hi << 32) | got_lo;
	match = (got_lo == want_lo) && (got_hi == want_hi);

	if (!match) {
		atomic_inc(&e->base_verify_fail);
		pr_err("mlpp: *** IN_DMA_BASE MISMATCH *** engine=%d src=%d\n",
		       eng, src);
		pr_err("mlpp:   wrote 0x%016llx (hi=0x%08x lo=0x%08x)\n",
		       (unsigned long long)in_base, want_hi, want_lo);
		pr_err("mlpp:   read  0x%016llx (hi=0x%08x lo=0x%08x)\n",
		       (unsigned long long)got, got_hi, got_lo);
		pr_err("mlpp:   xor   0x%016llx  (fails=%d of %d writes)\n",
		       (unsigned long long)(got ^ (u64)in_base),
		       atomic_read(&e->base_verify_fail),
		       atomic_read(&e->base_verify_writes) + 1);
	} else if (verify_base >= 2) {
		pr_info("mlpp: IN_DMA_BASE engine=%d src=%d wrote=0x%016llx read=0x%016llx OK\n",
			eng, src, (unsigned long long)in_base,
			(unsigned long long)got);
	}

	atomic_inc(&e->base_verify_writes);
	return match;
}

/* ---- BT.601 colour-space conversion coefficients (signed Q8) --------------
 * Selected per ROI by ml_pp_output_params_stored.color_from_csc:
 *   0 = RGB  -> YUV->RGB matrix, output packs as RGBX
 *   2 = BGR  -> YUV->BGR matrix (R/B rows swapped), output packs as BGRX
 *   else     -> CSC bypass (YUV passthrough), output packs as YUVX
 */
static const s32 YR_COEF[9]  = { 256, 0, 359,  256, -88, -183,  256, 454, 0 };
static const s32 YR_OFF[3]   = { -179, 136, -227 };
static const s32 BGR_COEF[9] = { 256, 454, 0,  256, -88, -183,  256, 0, 359 };
static const s32 BGR_OFF[3]  = { -227, 136, -179 };
static const s32 CSC_IDENTITY[9] = { 256, 0, 0,  0, 256, 0,  0, 0, 256 };
static const s32 CSC_ZERO[3] = { 0, 0, 0 };

/* ---- interrupt handlers ---------------------------------------------------
 * The completion lines are level-high but reach the PS through an edge-rising
 * axi_intc input, so each ISR drains its region's status to zero: clearing the
 * last bit drops the line and re-arms a fresh edge for anything set afterwards.
 *
 * When the accumulated done_mask shows the whole frame is complete, the ISR
 * publishes the outputs and signals frame_done (async pipeline completion).
 */
static void mlpp_mark_output_done(int dev, int slot); /* forward decl (defined below) */
static void mlpp_svc_try_dispatch(struct mlpp_engine *e); /* forward decl (defined below) */

/* Publish an in-flight frame's outputs once every done bit is set. Must be
 * called with e->lock held. */
static void mlpp_finalize_locked(struct mlpp_engine *e)
{
	u32 done_all = (1u << (1 + e->nroi)) - 1;
	int i;

	if (!e->busy || (e->done_mask & done_all) != done_all)
		return;

	for (i = 0; i < e->inflight_nroi; i++)
		mlpp_mark_output_done(e->inflight_devs[i], e->inflight_slots[i]);
	e->busy = false;
	complete(&e->frame_done);

	/* Frame is fully processed (input read + all ROI writes done). */
	if (e == &global_priv->engine[MLPP_ENGINE_FVC])	{
		PERF_PRINT("PIPE FVC, DONE X, NS=%lld\n", ktime_get_real_ns());
	}
	else if (e == &global_priv->engine[MLPP_ENGINE_SVC]){
		/* inflight_devs[0] = 12 + camera (svcN_out); the matching svcN_in
		 * device index is camera + 1 = inflight_devs[0] - 11. */
		PERF_PRINT("PIPE SVC, DONE %d, NS=%lld\n",
			   e->inflight_devs[0] - 11, ktime_get_real_ns());
	}

	/* Multi-source (SVC): release the camera that just finished so a blocked
	 * same-camera writer can proceed, then kick the queue to dispatch the next
	 * pending camera (deferred to process context via the workqueue). */
	if (e->multi_src && e->cur_src >= 0) {
		int src = e->cur_src;

		e->src_req[src].pending = false;
		e->cur_src = -1;
		complete(&e->src_done[src]);
		schedule_work(&e->dispatch_work);
	}
}

static irqreturn_t mlpp_isr_rd(struct mlpp_engine *e)
{
	u32 status, acc = 0;
	unsigned int guard = 0;

	/* Bounded drain. See MLPP_ISR_DRAIN_MAX: an unbounded loop here spins
	 * with IRQs off if any bit is not truly W1C. */
	while ((status = rdreg(e->rd, MLPP_ISR) & MLPP_RD_ISR_MASK)) {
		wrreg(e->rd, MLPP_ISR, status);      /* W1C */
		acc |= status;
		if (++guard >= MLPP_ISR_DRAIN_MAX) {
			/* A bit refuses to clear. Mask the line rather than
			 * livelock the CPU; the frame will time out and be
			 * reported, which is vastly better than a dead core. */
			wrreg(e->rd, MLPP_IER, 0);
			atomic_inc(&e->irq_drain_max);
			break;
		}
	}
	if (!acc) {
		atomic_inc(&e->irq_rd_none);
		/* Four handlers share an edge cascade, so empty entries are
		 * expected. Returning IRQ_NONE repeatedly invites the spurious
		 * IRQ detector to disable the line. */
		return IRQ_HANDLED;
	}
	atomic_inc(&e->irq_rd_entries);

	spin_lock(&e->lock);
	e->done_mask |= 1u;                          /* bit0 = input read done */
	mlpp_finalize_locked(e);
	spin_unlock(&e->lock);
	wake_up_interruptible(&e->wq);
	return IRQ_HANDLED;
}

static irqreturn_t mlpp_isr_wr(struct mlpp_engine *e)
{
	u32 wr_mask = (1u << e->nroi) - 1;
	u32 status;
	unsigned int guard = 0;
	bool any = false;

	/* Fold each drain pass into done_mask INSIDE the loop, under e->lock,
	 * and finalise there too.
	 *
	 * The previous version accumulated into a local `acc` and only OR'd it
	 * into done_mask after the loop exited. That leaves a window: the last
	 * ROI completion can land while the handler is still draining, and the
	 * frame is then only published on the NEXT interrupt -- which may never
	 * come, because that was the final ROI of the frame. Folding per pass
	 * means a frame is finalised the instant its last bit is observed.
	 *
	 * Safe to call mlpp_finalize_locked() repeatedly: it early-returns
	 * unless e->busy and the mask is complete, so complete()/the output
	 * publish still happen exactly once per frame. */
	while ((status = rdreg(e->wr, MLPP_ISR) & wr_mask)) {
		wrreg(e->wr, MLPP_ISR, status);      /* W1C */
		any = true;

		spin_lock(&e->lock);
		e->done_mask |= (status << 1);       /* ROI bit r -> done_mask[r+1] */
		mlpp_finalize_locked(e);
		spin_unlock(&e->lock);

		if (++guard >= MLPP_ISR_DRAIN_MAX) {
			wrreg(e->wr, MLPP_IER, 0);
			atomic_inc(&e->irq_drain_max);
			break;
		}
	}
	if (!any) {
		atomic_inc(&e->irq_wr_none);
		return IRQ_HANDLED;   /* shared edge cascade; see mlpp_isr_rd */
	}
	atomic_inc(&e->irq_wr_entries);

	wake_up_interruptible(&e->wq);
	return IRQ_HANDLED;
}

static irqreturn_t mlpp_isr_fvc_rd(int irq, void *dev) { return mlpp_isr_rd(dev); }
static irqreturn_t mlpp_isr_fvc_wr(int irq, void *dev) { return mlpp_isr_wr(dev); }
static irqreturn_t mlpp_isr_svc_rd(int irq, void *dev) { return mlpp_isr_rd(dev); }
static irqreturn_t mlpp_isr_svc_wr(int irq, void *dev) { return mlpp_isr_wr(dev); }

/* ---- per-ROI register programming ----------------------------------------- */
static void mlpp_program_roi(struct mlpp_engine *e, int r,
			     const struct ml_pp_output_params_stored *p,
			     dma_addr_t obase, u32 *csc_bypass)
{
	u32 base = MLPP_ROI_BASE(r);
	u32 roi_w = p->crop_xstop - p->crop_xstart;
	u32 roi_h = p->crop_ystop - p->crop_ystart;
	u32 step_x = p->width_out  ? (u32)(((u64)roi_w << 16) / p->width_out)  : 0;
	u32 step_y = p->height_out ? (u32)(((u64)roi_h << 16) / p->height_out) : 0;
	u32 sub = 0, cfg = 0;
	const s32 *coef, *off;
	u8 bypass;
	int i;

	DEBUG_PRINT("mlpp_program_roi: ROI%d  out=%ux%u  crop=(%u,%u)-(%u,%u) [roi %ux%u]  step=(0x%x,0x%x)  subtract_value=0x%08x  scale_value=0x%08x  color_from_csc=%u  color_out=%u\n",
		r, p->width_out, p->height_out,
		p->crop_xstart, p->crop_ystart, p->crop_xstop, p->crop_ystop,
		roi_w, roi_h, step_x, step_y,
		p->subtract_value, p->scale_value,
		p->color_from_csc, p->color_out);

	wrreg(e->wr, base + MLPP_ROI_START_XY,
	      ((p->crop_ystart & 0xFFFF) << 16) | (p->crop_xstart & 0xFFFF));
	wrreg(e->wr, base + MLPP_ROI_OUT_WH,
	      ((p->height_out & 0xFFFF) << 16) | (p->width_out & 0xFFFF));
	wrreg(e->wr, base + MLPP_ROI_STEP_X, step_x);
	wrreg(e->wr, base + MLPP_ROI_STEP_Y, step_y);

	/* subtract: packed {gy,bcb,rcr} in subtract_value; 0x80000000 = bypass. */
	if (p->subtract_value == 0x80000000) {
		cfg |= (1u << 12);                       /* sub_bypass */
	} else {
		u32 gy  =  p->subtract_value        & 0xFF;
		u32 bcb = (p->subtract_value >> 8)  & 0xFF;
		u32 rcr = (p->subtract_value >> 16) & 0xFF;

		sub = ((rcr & 0x1FF) << 18) | ((bcb & 0x1FF) << 9) | (gy & 0x1FF);
	}

	/* bitshift: packed per-channel shift+direction in scale_value;
	 * 0x80000000 = bypass. Repack into the new CFG layout. */
	if (p->scale_value == 0x80000000) {
		cfg |= (1u << 13);                       /* shift_bypass */
	} else {
		u32 sv = p->scale_value;
		u32 gy_sh  =  sv        & 0x7, gy_dir  = (sv >> 7)  & 1;
		u32 bcb_sh = (sv >> 8)  & 0x7, bcb_dir = (sv >> 15) & 1;
		u32 rcr_sh = (sv >> 16) & 0x7, rcr_dir = (sv >> 23) & 1;

		cfg |= gy_sh | (bcb_sh << 3) | (rcr_sh << 6) |
		       (gy_dir << 9) | (bcb_dir << 10) | (rcr_dir << 11);
	}

	wrreg(e->wr, base + MLPP_ROI_SUB, sub);
	/* NV12 packed output (display pipe / last ROI only; the output DMA
	 * decimates the UV plane). Requires CSC bypass, which color_from_csc=YUV
	 * selects below. */
	if (p->color_out == MLPP_COLOR_OUT_NV12)
		cfg |= (1u << 14);
	wrreg(e->wr, base + MLPP_ROI_CFG, cfg);
	wrreg(e->wr, base + MLPP_ROI_OBASE_LO, lower_32_bits(obase));
	wrreg(e->wr, base + MLPP_ROI_OBASE_HI, upper_32_bits(obase));

	switch (p->color_from_csc) {
	case 0:  coef = YR_COEF;      off = YR_OFF;   bypass = 0; break; /* RGB */
	case 2:  coef = BGR_COEF;     off = BGR_OFF;  bypass = 0; break; /* BGR */
	default: coef = CSC_IDENTITY; off = CSC_ZERO; bypass = 1; break; /* YUV */
	}
	for (i = 0; i < 9; i++)
		wrreg(e->wr, base + MLPP_ROI_CSC_COEF(i), (u32)coef[i]);
	for (i = 0; i < 3; i++)
		wrreg(e->wr, base + MLPP_ROI_CSC_OFFSET(i), (u32)off[i]);
	if (bypass)
		*csc_bypass |= BIT(r);
}

/* Which engine owns a given output device: FVC drives outputs 5..11, SVC 12..19. */
static struct mlpp_engine *mlpp_engine_for_out_dev(int dev)
{
	return (dev <= 11) ? &global_priv->engine[MLPP_ENGINE_FVC]
			   : &global_priv->engine[MLPP_ENGINE_SVC];
}

/* Reserve the next ring slot for an output device and return its DMA target.
 *
 * NON-BLOCKING drop-oldest. A single engine frame writes several output devices
 * (FVC: 7, SVC: 2); if this stalled whenever ONE output's ring were full it
 * would freeze the whole engine as soon as any consumer stopped draining. So
 * the producer never waits: when the target slot still holds an unread frame
 * (ring full), the oldest frame is dropped by pushing the reader forward, and
 * the slot is reused. Consumers that keep up lose nothing; slow/absent
 * consumers simply see the newest MLPP_OUT_NBUF-deep window. Runs under the
 * engine op_lock; only takes e->lock briefly. */
static dma_addr_t mlpp_output_reserve(struct mlpp_engine *e, int dev, int *slot_out)
{
	unsigned long flags;
	int slot;
	bool head_moved = false;

	spin_lock_irqsave(&e->lock, flags);

	/* NOTE: this deliberately does NOT skip slots pinned by an outstanding
	 * exported DMABUF. An earlier version did, which broke the pipeline: a
	 * consumer legitimately holding a few dmabuf fds starved the producer of
	 * slots, and rd_idx could park on a pinned slot that never gets
	 * buffer_written set again -- every later read() then hit the 2s timeout.
	 *
	 * Overwriting a slot userspace still holds can hand back a torn frame,
	 * but that is inherent to the drop-oldest design documented above and is
	 * a data-quality issue, not memory corruption. The memory-safety problem
	 * (CMA freed under a live mapping) is handled where it actually lives:
	 * ml_pp_write's resize and ml_pp_remove refuse/serialise via
	 * mlpp_dev_has_pinned_slots() and buf_sem. */
	slot = global_priv->wr_idx[dev];

	/* Ring full (this slot holds an unread frame): drop the oldest so the
	 * reader can't consume the slot we are about to overwrite. */
	if (global_priv->buffer_written[dev][slot] &&
	    global_priv->rd_idx[dev] == slot) {
		global_priv->rd_idx[dev] = (slot + 1) % MLPP_OUT_NBUF;
		head_moved = true;
		atomic_inc(&global_priv->stat_drops[dev]);
	}
	global_priv->buffer_written[dev][slot] = false;   /* in flight until completion */
	global_priv->wr_idx[dev] = (slot + 1) % MLPP_OUT_NBUF;
	spin_unlock_irqrestore(&e->lock, flags);

	/* Moving rd_idx changes what mlpp_output_head_ready() answers, so any
	 * consumer already asleep must re-evaluate against the NEW head. Without
	 * this the sleeper is not re-woken until some later completion happens
	 * to fire, which is how a consumer ended up waiting out its full 2s
	 * timeout with a ready frame sitting in the ring. */
	if (head_moved)
		wake_up_interruptible(&e->wq);

	*slot_out = slot;
	return global_priv->dma_handle_fb[dev][slot];
}

/* Take exclusive control of the engine that writes this output device, so its
 * CMA slots can be freed and reallocated safely.
 *
 * Holding op_lock is what makes this safe: mlpp_run_frame() and
 * mlpp_svc_try_dispatch() both take it before touching MLPP_ROI_OBASE_*, so no
 * new frame can be programmed with the old addresses while we hold it. Waiting
 * on e->busy alone is NOT sufficient -- the SVC engine keeps a per-camera queue
 * (src_req[].pending) that the completion workqueue dispatches the moment busy
 * clears.
 *
 * Pair with mlpp_output_engine_unlock(). May sleep. */
void mlpp_output_engine_lock(int dev)
{
	struct mlpp_engine *e = mlpp_engine_for_out_dev(dev);
	int idx = (e == &global_priv->engine[MLPP_ENGINE_FVC]) ? MLPP_ENGINE_FVC
							       : MLPP_ENGINE_SVC;
	unsigned long flags;
	bool busy;

	/* Stop the SVC dispatch worker from queuing anything new behind us. */
	if (idx == MLPP_ENGINE_SVC)
		cancel_work_sync(&e->dispatch_work);

	mutex_lock(&e->op_lock);

	/* Let the frame that is already in the hardware land. op_lock is held, so
	 * nothing can start another one behind this wait. */
	spin_lock_irqsave(&e->lock, flags);
	busy = e->busy;
	spin_unlock_irqrestore(&e->lock, flags);
	if (busy &&
	    !wait_for_completion_timeout(&e->frame_done, msecs_to_jiffies(2000)))
		pr_warn("mlpp: engine %d did not go idle before buffer resize\n", idx);
}

void mlpp_output_engine_unlock(int dev)
{
	struct mlpp_engine *e = mlpp_engine_for_out_dev(dev);
	int idx = (e == &global_priv->engine[MLPP_ENGINE_FVC]) ? MLPP_ENGINE_FVC
							       : MLPP_ENGINE_SVC;

	mutex_unlock(&e->op_lock);

	/* Any camera frames that queued up while we held the lock can go now. */
	if (idx == MLPP_ENGINE_SVC)
		mlpp_svc_try_dispatch(e);
}

/* Consumer claims the frame at the head of the ring for export as a DMABUF:
 * pin the slot and advance rd_idx as one atomic step, so the producer's
 * drop-oldest cannot move rd_idx between the capture and the consume.
 * Returns the pinned slot, or -1 if the head slot holds no completed frame.
 * The caller must arrange for mlpp_slot_unpin() when the export dies. */
int mlpp_output_claim_for_export(int dev)
{
	struct mlpp_engine *e = mlpp_engine_for_out_dev(dev);
	unsigned long flags;
	int slot;

	spin_lock_irqsave(&e->lock, flags);

	slot = global_priv->rd_idx[dev];
	if (!global_priv->buffer_written[dev][slot]) {
		spin_unlock_irqrestore(&e->lock, flags);
		return -1;
	}

	/* No cap on how many slots may be pinned. An earlier version refused at
	 * NBUF-1 to guarantee the producer a free slot, but that made a normal
	 * consumer holding several fds get -EAGAIN, and combined with the
	 * (now removed) pinned-slot skip in mlpp_output_reserve() it stalled the
	 * pipeline. The pin count is now advisory: it tells the resize and
	 * remove paths that userspace still has mappings, and nothing else. */
	atomic_inc(&global_priv->slot_pinned[dev][slot]);
	global_priv->buffer_written[dev][slot] = false;
	global_priv->rd_idx[dev] = (slot + 1) % MLPP_OUT_NBUF;

	spin_unlock_irqrestore(&e->lock, flags);

	/* Head moved -- see mlpp_output_consumed(). */
	wake_up_interruptible(&e->wq);
	return slot;
}

/* Consumer finished a whole frame from dev's current read slot: free it and
 * advance the ring to the next frame. */
void mlpp_output_consumed(int dev)
{
	struct mlpp_engine *e = mlpp_engine_for_out_dev(dev);
	unsigned long flags;
	int slot;

	spin_lock_irqsave(&e->lock, flags);
	slot = global_priv->rd_idx[dev];
	global_priv->buffer_written[dev][slot] = false;
	global_priv->rd_idx[dev] = (slot + 1) % MLPP_OUT_NBUF;
	spin_unlock_irqrestore(&e->lock, flags);

	/* The head moved: re-evaluate any sleeping consumer against the new
	 * head, and let a producer blocked on a full ring proceed. */
	wake_up_interruptible(&e->wq);
}

/* True when the CURRENT head of dev's ring holds a completed frame.
 *
 * rd_idx must be re-read on every evaluation. See the bug note in
 * mlpp_output_wait_ready_timeout(). */
static bool mlpp_output_head_ready(int dev)
{
	int slot = READ_ONCE(global_priv->rd_idx[dev]);

	if (slot < 0 || slot >= MLPP_OUT_NBUF)
		return false;
	return READ_ONCE(global_priv->buffer_written[dev][slot]);
}

/* Block until the ring slot at rd_idx[dev] holds a completed frame.
 *
 * rd_idx is NOT stable across this wait: the producer's drop-oldest in
 * mlpp_output_reserve() advances it too. The predicate therefore re-reads
 * rd_idx on EVERY evaluation.
 *
 * BUG FIXED (2026-09-14): this used to latch `slot = rd_idx` once, before
 * wait_event_interruptible_timeout(), and then sleep on
 * buffer_written[dev][<that slot>]. When drop-oldest moved rd_idx while the
 * consumer slept, the sleeper kept testing a slot that was no longer the head
 * and that nothing would ever set again -- so it burned the full 2s and
 * returned -ETIMEDOUT *while completed frames sat in the ring*. That is the
 * "Timeout waiting for output ring slot for device N" seen on devices 11/16,
 * which wedges the consumer and back-pressures the whole capture pipeline.
 *
 * Do NOT "fix" the instability by making the producer skip slots to keep
 * rd_idx pinned in place: a slot the producer refuses to write never gets
 * buffer_written set, and every later wait then times out. That was a
 * previous, separate regression.
 *
 * Returns 0 on success or -ETIMEDOUT / -ERESTARTSYS. */
int mlpp_output_wait_ready_timeout(int dev, unsigned int timeout_ms)
{
	struct mlpp_engine *e = mlpp_engine_for_out_dev(dev);
	long ret;

	/* Fast path: already ready, do not touch the wait queue at all. */
	if (mlpp_output_head_ready(dev))
		return 0;

	ret = wait_event_interruptible_timeout(e->wq,
			mlpp_output_head_ready(dev),
			msecs_to_jiffies(timeout_ms));
	if (ret == 0) {
		/* Final re-check: a completion that lands between the last
		 * predicate evaluation and the timer expiring must not be
		 * reported as a timeout. */
		if (mlpp_output_head_ready(dev))
			return 0;
		atomic_inc(&global_priv->stat_wait_timeouts[dev]);
		return -ETIMEDOUT;
	}
	if (ret < 0)
		return ret;
	
	//test delay

	return 0;
}

int mlpp_output_wait_ready(int dev)
{
	return mlpp_output_wait_ready_timeout(dev, 2000);
}

/* Input ping-pong (slot 0/1) helpers. The index is kept in
 * current_write_buffer[dev]; the output ring uses wr_idx/rd_idx instead. */
void *mlpp_input_write_cpu(int dev)
{
	return global_priv->cpu_addr_fb[dev][global_priv->current_write_buffer[dev]];
}
static dma_addr_t mlpp_input_write_dma(int dev)
{
	return global_priv->dma_handle_fb[dev][global_priv->current_write_buffer[dev]];
}

/* Block until the engine's in-flight frame (if any) has completed. Used before
 * replacing an input DMABUF so the hardware never reads a buffer we unmap. */
void mlpp_wait_engine_idle(int engine_idx)
{
	struct mlpp_engine *e = &global_priv->engine[engine_idx];
	unsigned long flags;
	bool busy;

	spin_lock_irqsave(&e->lock, flags);
	busy = e->busy;
	spin_unlock_irqrestore(&e->lock, flags);
	if (busy &&
	    !wait_for_completion_timeout(&e->frame_done, msecs_to_jiffies(2000)))
		pr_warn("mlpp: engine %d idle wait timed out\n", engine_idx);
}

/* Mark the ring slot this frame wrote as filled and wake readers (both the
 * blocking read() path via read_complete and any waiter on e->wq). The consumer
 * advances rd_idx itself once it has drained the slot (mlpp_output_consumed). */
static void mlpp_mark_output_done(int dev, int slot)
{
	WRITE_ONCE(global_priv->buffer_written[dev][slot], true);
	global_priv->read_ready[dev] = true;
	atomic_inc(&global_priv->stat_frames_done[dev]);
	complete(&global_priv->read_complete[dev]);
}

/* Program one whole frame (input + all ROIs) and kick GO. In IRQ mode this is
 * ASYNCHRONOUS: it waits only for the PREVIOUS frame to finish, then returns as
 * soon as GO is issued -- the completion ISR publishes the outputs. This lets
 * the caller queue the next input while the hardware runs. In polling mode
 * (no IRQs) it falls back to running the frame synchronously. */
static int mlpp_run_frame(struct mlpp_engine *e, u32 in_w, u32 in_h,
			  dma_addr_t in_base, const int *out_devs)
{
	u32 nroi = e->nroi;
	u32 wr_mask = (1u << nroi) - 1;
	u32 done_all = (1u << (1 + nroi)) - 1;
	bool use_irq = (e->irq_rd > 0) && (e->irq_wr > 0);
	unsigned long flags;
	u32 csc_bypass = 0;
	bool was_busy;
	int slots[MLPP_FVC_NROI];
	int r;

	mutex_lock(&e->op_lock);

	/* Backpressure: wait for the PREVIOUS in-flight frame to finish before
	 * reprogramming (the hardware is a single instance). This is the only
	 * place the caller can block, and only if it outpaces the hardware. */
	if (use_irq) {
		spin_lock_irqsave(&e->lock, flags);
		was_busy = e->busy;
		spin_unlock_irqrestore(&e->lock, flags);
		if (was_busy &&
		    !wait_for_completion_timeout(&e->frame_done, msecs_to_jiffies(2000)))
			pr_warn("mlpp: previous frame timed out\n");
	}

	DEBUG_PRINT("mlpp_run_frame: nroi=%u  INPUT_HSIZE=%u INPUT_VSIZE=%u  in_base=0x%llx\n",
		nroi, in_w, in_h, (unsigned long long)in_base);

	/* input frame geometry + base */
	wrreg(e->rd, MLPP_INPUT_HSIZE, in_w);
	wrreg(e->rd, MLPP_INPUT_VSIZE, in_h);
	if (verify_base)
		(void)mlpp_write_in_base_verify(e, -1, in_base);
	else {
		wrreg(e->rd, MLPP_IN_DMA_BASE_LO, lower_32_bits(in_base));
		wrreg(e->rd, MLPP_IN_DMA_BASE_HI, upper_32_bits(in_base));
	}

	for (r = 0; r < nroi; r++) {
		dma_addr_t obase = mlpp_output_reserve(e, out_devs[r], &slots[r]);

		mlpp_program_roi(e, r, mlpp_params_lookup[out_devs[r]],
				 obase, &csc_bypass);
	}
	wrreg(e->wr, MLPP_CSC_BYPASS, csc_bypass);

	/* arm: record the in-flight frame, clear stale done state, enable IRQs */
	spin_lock_irqsave(&e->lock, flags);
	e->done_mask = 0;
	for (r = 0; r < nroi; r++) {
		int dev = out_devs[r];

		e->inflight_devs[r] = dev;
		e->inflight_slots[r] = slots[r];
	}
	e->inflight_nroi = nroi;
	reinit_completion(&e->frame_done);
	e->busy = true;
	spin_unlock_irqrestore(&e->lock, flags);

	wrreg(e->rd, MLPP_ISR, MLPP_RD_ISR_MASK);
	wrreg(e->wr, MLPP_ISR, wr_mask);
	wrreg(e->rd, MLPP_IER, use_irq ? MLPP_RD_ISR_MASK : 0);
	wrreg(e->wr, MLPP_IER, use_irq ? wr_mask : 0);
	wmb();

	/* kick: enable + GO (self-clearing) on the RD region */
	wrreg(e->rd, MLPP_CTRL, MLPP_CTRL_ENABLE | MLPP_CTRL_GO);

	if (use_irq) {
		/* Asynchronous: return now; the completion ISR finalises. */
		mutex_unlock(&e->op_lock);
		return 0;
	}

	/* Polling fallback (no usable IRQs): run this frame synchronously. */
	{
		unsigned long deadline = jiffies + msecs_to_jiffies(2000);
		u32 acc = 0;

		do {
			u32 rd = rdreg(e->rd, MLPP_ISR) & MLPP_RD_ISR_MASK;
			u32 wr = rdreg(e->wr, MLPP_ISR) & wr_mask;

			if (rd)
				wrreg(e->rd, MLPP_ISR, rd);
			if (wr)
				wrreg(e->wr, MLPP_ISR, wr);
			acc |= (rd ? 1u : 0u) | (wr << 1);

			if ((acc & done_all) == done_all)
				break;
			if (time_after(jiffies, deadline))
				break;
			usleep_range(50, 200);
		} while (1);

		wrreg(e->rd, MLPP_IER, 0);
		wrreg(e->wr, MLPP_IER, 0);

		spin_lock_irqsave(&e->lock, flags);
		e->done_mask |= acc;
		mlpp_finalize_locked(e);
		spin_unlock_irqrestore(&e->lock, flags);

		/* Both ISRs wake e->wq after finalising; the polling fallback
		 * must too, or a consumer sleeping in
		 * mlpp_output_wait_ready_timeout() is never told the frame it
		 * is waiting for has landed and times out at 2s. */
		wake_up_interruptible(&e->wq);

		mutex_unlock(&e->op_lock);

		if ((acc & done_all) != done_all) {
			pr_warn("mlpp: frame timed out, done_mask=0x%02x (want 0x%02x)\n",
				acc, done_all);
			return -ETIMEDOUT;
		}
	}
	return 0;
}

/* ---- multi-source (SVC) non-blocking queue --------------------------------
 * The single SVC engine is shared by cameras svc0..3. To avoid serialising
 * writes to different cameras, each camera has one queued frame slot. A write
 * enqueues its frame and, if the engine is idle, kicks it immediately;
 * otherwise the completion path dispatches the next pending camera. A write to
 * a camera whose frame is still outstanding blocks (see mlpp_svc_wait_src_idle).
 */

/* Program + GO the next pending source if the engine is free. Runs in process
 * context (input write path or the dispatch workqueue). */
static void mlpp_svc_try_dispatch(struct mlpp_engine *e)
{
	bool use_irq = (e->irq_rd > 0) && (e->irq_wr > 0);
	u32 csc_bypass = 0;
	unsigned long flags;
	struct mlpp_frame_req req;
	int slots[MLPP_SVC_NROI];
	int src = -1, i, k, r;
	u32 wr_mask;

	mutex_lock(&e->op_lock);

	spin_lock_irqsave(&e->lock, flags);
	if (e->busy) {
		spin_unlock_irqrestore(&e->lock, flags);
		mutex_unlock(&e->op_lock);
		return;
	}
	/* round-robin scan so no camera starves */
	for (k = 0; k < MLPP_SVC_NSRC; k++) {
		i = (e->rr_next + k) % MLPP_SVC_NSRC;
		if (e->src_req[i].pending) {
			src = i;
			break;
		}
	}
	if (src < 0) {
		spin_unlock_irqrestore(&e->lock, flags);
		mutex_unlock(&e->op_lock);
		return;
	}
	req = e->src_req[src];                 /* snapshot geometry/base/out_devs */
	e->cur_src = src;
	e->rr_next = (src + 1) % MLPP_SVC_NSRC;
	e->done_mask = 0;
	e->busy = true;                        /* reserve before we program/GO */
	reinit_completion(&e->frame_done);
	spin_unlock_irqrestore(&e->lock, flags);

	wr_mask = (1u << req.nroi) - 1;

	/* input frame geometry + base */
	wrreg(e->rd, MLPP_INPUT_HSIZE, req.in_w);
	wrreg(e->rd, MLPP_INPUT_VSIZE, req.in_h);
	if (verify_base)
		(void)mlpp_write_in_base_verify(e, src, req.in_base);
	else {
		wrreg(e->rd, MLPP_IN_DMA_BASE_LO, lower_32_bits(req.in_base));
		wrreg(e->rd, MLPP_IN_DMA_BASE_HI, upper_32_bits(req.in_base));
	}

	for (r = 0; r < req.nroi; r++) {
		dma_addr_t obase = mlpp_output_reserve(e, req.out_devs[r], &slots[r]);

		mlpp_program_roi(e, r, mlpp_params_lookup[req.out_devs[r]],
				 obase, &csc_bypass);
	}
	wrreg(e->wr, MLPP_CSC_BYPASS, csc_bypass);

	/* record in-flight outputs + the ring slot each one wrote to */
	spin_lock_irqsave(&e->lock, flags);
	for (r = 0; r < req.nroi; r++) {
		e->inflight_devs[r] = req.out_devs[r];
		e->inflight_slots[r] = slots[r];
	}
	e->inflight_nroi = req.nroi;
	spin_unlock_irqrestore(&e->lock, flags);

	wrreg(e->rd, MLPP_ISR, MLPP_RD_ISR_MASK);
	wrreg(e->wr, MLPP_ISR, wr_mask);
	wrreg(e->rd, MLPP_IER, use_irq ? MLPP_RD_ISR_MASK : 0);
	wrreg(e->wr, MLPP_IER, use_irq ? wr_mask : 0);
	wmb();

	wrreg(e->rd, MLPP_CTRL, MLPP_CTRL_ENABLE | MLPP_CTRL_GO);
	PERF_PRINT("PIPE SVC-GO, START %d, NS=%lld\n", src, ktime_get_real_ns());

	mutex_unlock(&e->op_lock);
}

/* Workqueue entry: dispatch the next queued SVC frame after a completion. */
static void mlpp_svc_dispatch_work(struct work_struct *w)
{
	struct mlpp_engine *e = container_of(w, struct mlpp_engine, dispatch_work);

	mlpp_svc_try_dispatch(e);
}

/* Block until a specific SVC camera's outstanding frame (if any) completes.
 * Only this camera is waited on; other cameras keep running. */
void mlpp_svc_wait_src_idle(int camera)
{
	struct mlpp_engine *e = &global_priv->engine[MLPP_ENGINE_SVC];
	unsigned long flags;
	bool pending;

	if (camera < 0 || camera >= MLPP_SVC_NSRC)
		return;

	spin_lock_irqsave(&e->lock, flags);
	pending = e->src_req[camera].pending;
	spin_unlock_irqrestore(&e->lock, flags);

	if (pending &&
	    !wait_for_completion_timeout(&e->src_done[camera], msecs_to_jiffies(2000)))
		pr_warn("mlpp: SVC camera %d idle wait timed out\n", camera);
}

/* ---- high-level per-frame triggers (called from the input write paths) ---- */
int mlpp_process_fvc_frame(void)
{
	static const int outs[MLPP_FVC_NROI] = { 5, 6, 7, 8, 9, 10, 11 };
	int in_dev = 0;
	unsigned long iflags;
	bool dmabuf;
	dma_addr_t base;
	int ret;

	/* Read the DMABUF flag and its base address as one atomic pair under the
	 * lock that ml_pp_cleanup_dmabuf_locked() uses to clear them. Reading them
	 * separately lets a concurrent close() zero the address in between, which
	 * would program the engine to DMA from physical address 0. */
	spin_lock_irqsave(&global_priv->lock[0], iflags);
	dmabuf = global_priv->using_dmabuf[in_dev];
	base = dmabuf ? global_priv->dmabuf_plane1_addr[in_dev] : 0;
	spin_unlock_irqrestore(&global_priv->lock[0], iflags);

	if (dmabuf && !base) {
		pr_warn("mlpp: FVC input DMABUF went away before frame start\n");
		return -EAGAIN;
	}
	if (!dmabuf)
		base = mlpp_input_write_dma(in_dev);

	DEBUG_PRINT("mlpp_process_fvc_frame: in=%ux%u  dmabuf=%d  inbuf=%d  base=0x%llx\n",
		mlpp_input_params[in_dev].width_in, mlpp_input_params[in_dev].height_in,
		dmabuf, global_priv->current_write_buffer[in_dev],
		(unsigned long long)base);

	ret = mlpp_run_frame(&global_priv->engine[MLPP_ENGINE_FVC],
			     mlpp_input_params[in_dev].width_in,
			     mlpp_input_params[in_dev].height_in, base, outs);

	/* Flip the input A/B buffer so the next frame is filled into the other
	 * buffer while this one is processed. (DMABUF supplies its own buffers.) */
	if (!dmabuf)
		global_priv->current_write_buffer[in_dev] ^= 1;

	return ret;
}

int mlpp_process_svc_frame(int camera)
{
	struct mlpp_engine *e = &global_priv->engine[MLPP_ENGINE_SVC];
	bool use_irq = (e->irq_rd > 0) && (e->irq_wr > 0);
	int in_dev = 1 + camera;                 /* svc0..3_in = devices 1..4 */
	unsigned long flags;
	unsigned long iflags;
	bool dmabuf;
	dma_addr_t base;

	if (camera < 0 || camera > 3)
		return -EINVAL;

	/* Block only if THIS camera still has a frame outstanding; other cameras
	 * are unaffected. (No-op in polling mode, where nothing is queued.) */
	mlpp_svc_wait_src_idle(camera);

	/* Atomically pair the DMABUF flag with its base address -- see the same
	 * comment in mlpp_process_fvc_frame(). */
	spin_lock_irqsave(&global_priv->lock[0], iflags);
	dmabuf = global_priv->using_dmabuf[in_dev];
	base = dmabuf ? global_priv->dmabuf_plane1_addr[in_dev] : 0;
	spin_unlock_irqrestore(&global_priv->lock[0], iflags);

	if (dmabuf && !base) {
		pr_warn("mlpp: SVC camera %d input DMABUF went away before frame start\n",
			camera);
		return -EAGAIN;
	}
	if (!dmabuf)
		base = mlpp_input_write_dma(in_dev);

	DEBUG_PRINT("mlpp_process_svc_frame: cam=%d in_dev=%d  in=%ux%u  color_in=%u  dmabuf=%d  inbuf=%d  base=0x%llx\n",
		camera, in_dev,
		mlpp_input_params[in_dev].width_in, mlpp_input_params[in_dev].height_in,
		mlpp_input_params[in_dev].color_in, dmabuf,
		global_priv->current_write_buffer[in_dev], (unsigned long long)base);

	if (!use_irq) {
		/* No completion IRQs: run this frame synchronously (the async queue
		 * relies on the completion ISR to advance). */
		int outs[MLPP_SVC_NROI] = { 12 + camera, 16 + camera };
		int ret = mlpp_run_frame(e, mlpp_input_params[in_dev].width_in,
					 mlpp_input_params[in_dev].height_in, base, outs);

		if (!dmabuf)
			global_priv->current_write_buffer[in_dev] ^= 1;
		return ret;
	}

	/* Queue this camera's frame (one outstanding per camera). */
	spin_lock_irqsave(&e->lock, flags);
	e->src_req[camera].in_w        = mlpp_input_params[in_dev].width_in;
	e->src_req[camera].in_h        = mlpp_input_params[in_dev].height_in;
	e->src_req[camera].in_base     = base;
	e->src_req[camera].out_devs[0] = 12 + camera;   /* svcN_out     -> ROI 0 */
	e->src_req[camera].out_devs[1] = 16 + camera;   /* svcN_display -> ROI 1 */
	e->src_req[camera].nroi        = MLPP_SVC_NROI;
	e->src_req[camera].pending     = true;
	reinit_completion(&e->src_done[camera]);
	spin_unlock_irqrestore(&e->lock, flags);

	/* Advance the input A/B buffer for the next fill (DMABUF has its own). */
	if (!dmabuf)
		global_priv->current_write_buffer[in_dev] ^= 1;

	/* Kick now if the engine is free; otherwise the completion path will
	 * dispatch this frame when the engine drains. Non-blocking either way. */
	mlpp_svc_try_dispatch(e);
	return 0;
}

/* ---- probe/remove helpers ------------------------------------------------- */
int mlpp_hw_map_and_request(struct platform_device *pdev, struct ml_pp_priv *priv)
{
	struct mlpp_engine *fvc = &priv->engine[MLPP_ENGINE_FVC];
	struct mlpp_engine *svc = &priv->engine[MLPP_ENGINE_SVC];
	int ret;

	for (int i = 0; i < MLPP_NUM_ENGINES; i++) {
		init_waitqueue_head(&priv->engine[i].wq);
		spin_lock_init(&priv->engine[i].lock);
		mutex_init(&priv->engine[i].op_lock);
		init_completion(&priv->engine[i].frame_done);
		priv->engine[i].done_mask = 0;
		priv->engine[i].busy = false;
		priv->engine[i].inflight_nroi = 0;
		priv->engine[i].irq_rd = -1;
		priv->engine[i].irq_wr = -1;
		atomic_set(&priv->engine[i].irq_rd_entries, 0);
		atomic_set(&priv->engine[i].irq_wr_entries, 0);
		atomic_set(&priv->engine[i].irq_rd_none, 0);
		atomic_set(&priv->engine[i].irq_wr_none, 0);
		atomic_set(&priv->engine[i].irq_drain_max, 0);

		/* Multi-source dispatch queue (used by SVC; harmless for FVC). */
		priv->engine[i].multi_src = false;
		priv->engine[i].cur_src = -1;
		priv->engine[i].rr_next = 0;
		INIT_WORK(&priv->engine[i].dispatch_work, mlpp_svc_dispatch_work);
		for (int s = 0; s < MLPP_SVC_NSRC; s++) {
			priv->engine[i].src_req[s].pending = false;
			init_completion(&priv->engine[i].src_done[s]);
		}
	}
	fvc->nroi = MLPP_FVC_NROI;
	svc->nroi = MLPP_SVC_NROI;
	svc->multi_src = true;   /* SVC uses the per-camera queue */

	fvc->rd = devm_platform_ioremap_resource_byname(pdev, "fvc_rd");
	if (IS_ERR(fvc->rd))
		return PTR_ERR(fvc->rd);
	fvc->wr = devm_platform_ioremap_resource_byname(pdev, "fvc_wr");
	if (IS_ERR(fvc->wr))
		return PTR_ERR(fvc->wr);
	svc->rd = devm_platform_ioremap_resource_byname(pdev, "svc_rd");
	if (IS_ERR(svc->rd))
		return PTR_ERR(svc->rd);
	svc->wr = devm_platform_ioremap_resource_byname(pdev, "svc_wr");
	if (IS_ERR(svc->wr))
		return PTR_ERR(svc->wr);

	/* mask all interrupts before requesting the lines */
	wrreg(fvc->rd, MLPP_IER, 0);
	wrreg(fvc->wr, MLPP_IER, 0);
	wrreg(svc->rd, MLPP_IER, 0);
	wrreg(svc->wr, MLPP_IER, 0);

	if (!poll_mode) {
		fvc->irq_rd = platform_get_irq_byname_optional(pdev, "fvc_rd");
		fvc->irq_wr = platform_get_irq_byname_optional(pdev, "fvc_wr");
		svc->irq_rd = platform_get_irq_byname_optional(pdev, "svc_rd");
		svc->irq_wr = platform_get_irq_byname_optional(pdev, "svc_wr");
	}

	if (fvc->irq_rd > 0) {
		ret = devm_request_irq(priv->dev, fvc->irq_rd, mlpp_isr_fvc_rd, 0,
				       "mlpp-fvc-rd", fvc);
		if (ret) {
			dev_err(priv->dev, "request fvc_rd irq failed: %d\n", ret);
			fvc->irq_rd = -1;
		}
	}
	if (fvc->irq_wr > 0) {
		ret = devm_request_irq(priv->dev, fvc->irq_wr, mlpp_isr_fvc_wr, 0,
				       "mlpp-fvc-wr", fvc);
		if (ret) {
			dev_err(priv->dev, "request fvc_wr irq failed: %d\n", ret);
			fvc->irq_wr = -1;
		}
	}
	if (svc->irq_rd > 0) {
		ret = devm_request_irq(priv->dev, svc->irq_rd, mlpp_isr_svc_rd, 0,
				       "mlpp-svc-rd", svc);
		if (ret) {
			dev_err(priv->dev, "request svc_rd irq failed: %d\n", ret);
			svc->irq_rd = -1;
		}
	}
	if (svc->irq_wr > 0) {
		ret = devm_request_irq(priv->dev, svc->irq_wr, mlpp_isr_svc_wr, 0,
				       "mlpp-svc-wr", svc);
		if (ret) {
			dev_err(priv->dev, "request svc_wr irq failed: %d\n", ret);
			svc->irq_wr = -1;
		}
	}

	dev_info(priv->dev,
		 "mlpp-v2 mapped: FVC ver=0x%08x SVC ver=0x%08x (irq fvc rd=%d wr=%d, svc rd=%d wr=%d)\n",
		 rdreg(fvc->wr, MLPP_VERSION), rdreg(svc->wr, MLPP_VERSION),
		 fvc->irq_rd, fvc->irq_wr, svc->irq_rd, svc->irq_wr);
	return 0;
}

void mlpp_hw_release(struct ml_pp_priv *priv)
{
	for (int i = 0; i < MLPP_NUM_ENGINES; i++) {
		struct mlpp_engine *e = &priv->engine[i];

		if (e->rd) {
			wrreg(e->rd, MLPP_IER, 0);
			wrreg(e->rd, MLPP_CTRL, 0);
		}
		if (e->wr)
			wrreg(e->wr, MLPP_IER, 0);

		/* Ensure no queued dispatch runs after the hardware is masked. */
		cancel_work_sync(&e->dispatch_work);
	}
	/* devm_ owns the ioremap/irq resources; freed automatically on unbind. */
}