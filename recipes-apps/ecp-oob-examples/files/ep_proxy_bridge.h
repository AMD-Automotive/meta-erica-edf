// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * ep_proxy_bridge.h
 * In-process PCIe frame-sharing bridge for the MDB5 endpoint.
 *
 * Adapted from the standalone ep-proxy program so that this application can
 * publish each frame it captures locally (for MLPP + DRM display) over PCIe
 * to the host at the same time, without a second process owning the V4L2
 * devices.
 *
 * For each captured frame the bridge writes the frame's DDR physical address
 * and metadata into the shared BRAM mailbox and fires an MSI-X interrupt so
 * the host can eDMA the frame.  Publishing is NON-BLOCKING: it never waits for
 * the host ACK, so the real-time local display pipeline is never stalled.
 *
 * Author: LT Thomas
 */
#ifndef EP_PROXY_BRIDGE_H
#define EP_PROXY_BRIDGE_H

#include <cstddef>
#include <cstdint>

#include "mdb5_bridge.h"

class EpProxyBridge {
public:
    /* Max capture buffers per channel we cache physical addresses for. */
    static const int MAX_BUFFERS = 8;

    EpProxyBridge();
    ~EpProxyBridge();

    /*
     * Map the BRAM mailbox and MSI-X trigger register via /dev/mem
     * (requires root) and clear the mailbox region.
     *
     *   num_channels : total number of channels advertised to the host.
     *   stamp_seq    : stamp an in-band watermark into the first bytes of
     *                  each frame (overwrites top-left pixels).  Off by
     *                  default so the locally-displayed image stays clean.
     *
     * Returns false (and leaves isReady() == false) if the hardware could not
     * be mapped; the caller can then run in local-only mode.
     */
    bool init(int num_channels, bool stamp_seq = false);

    bool isReady() const { return ready_; }

    /* Write a channel's static format metadata into its BRAM slot. */
    bool registerChannel(int ch, uint32_t width, uint32_t height,
                         uint32_t fourcc, uint32_t bytesperline,
                         uint32_t sizeimage);

    /*
     * Discover and cache the physical DDR address of a capture buffer using
     * a CMA magic-pattern scan through /dev/mem.  Must be called once per
     * (ch, buf_index) BEFORE the buffer holds real frame data (i.e. before
     * VIDIOC_STREAMON), because it briefly writes a magic pattern into the
     * buffer.  'va' is the buffer's mmap'd CPU address.
     *
     * Returns the physical address, or 0 on failure.
     */
    uint64_t discoverPhys(int ch, int buf_index, void *va, size_t len);

    /*
     * Publish a freshly captured frame: (optionally) stamp the watermark,
     * write the mailbox metadata, memory-barrier, raise VALID and fire MSI-X.
     * Non-blocking.  Uses the physical address cached by discoverPhys() for
     * (ch, buf_index); if that address is unknown the frame is skipped.
     */
    void publishFrame(int ch, int buf_index, void *va, uint32_t size);

    /*
     * Write the mailbox header magic so the host begins discovery.  Call once
     * after every channel has been registered.
     */
    void finalize();

    /* Tear down: mark endpoint not-ready and release mappings. */
    void shutdown();

private:
    int mem_fd_;

    void *bram_map_base_;
    size_t bram_map_len_;
    void *msix_map_base_;
    size_t msix_map_len_;

    volatile void *bram_;
    volatile struct mdb5_mbox_header *bram_header_;
    volatile uint32_t *msix_reg_;

    volatile struct mdb5_mbox_slot *slots_[MDB5_MAX_CHANNELS];
    uint64_t phys_[MDB5_MAX_CHANNELS][MAX_BUFFERS];
    uint32_t frame_seq_[MDB5_MAX_CHANNELS];

    int num_channels_;
    bool stamp_seq_;
    bool ready_;
    bool finalized_;
};

#endif /* EP_PROXY_BRIDGE_H */
