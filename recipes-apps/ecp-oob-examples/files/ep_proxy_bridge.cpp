// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: LT
// Maintainer: LT

/**
 * ep_proxy_bridge.cpp
 * In-process PCIe frame-sharing bridge for the MDB5 endpoint.
 * See ep_proxy_bridge.h for an overview.
 *
 * Adapted from ep-proxy/ep_proxy.c (map_hardware, init_bram,
 * discover_phys_addr and the per-frame mailbox publish path).
 *
 * Author: LT Thomas
 */

#include "ep_proxy_bridge.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/*
 * Versal CMA reserved-memory window (from the device-tree reserved-memory
 * node).  vb2-dma-contig / dma-heap CMA buffers land somewhere in this
 * window, so the magic-pattern scan must cover the whole region.
 */
#define EPB_CMA_BASE        0x800000000ULL
#define EPB_CMA_SCAN_SIZE   (5ULL * 1024 * 1024 * 1024)  /* 5GB */
#define EPB_CMA_SCAN_CHUNK  (2ULL * 1024 * 1024)         /* 2MB per mmap */
#define EPB_PAGE_SIZE       4096ULL

namespace {

/* 16-byte magic written at the start of a buffer for phys addr discovery. */
struct phys_magic {
    uint64_t sig;       /* 0xDEADBEEF_EP_PROXY | buf_index */
    uint32_t buf_index; /* which buffer */
    uint32_t checksum;  /* simple validation */
};

const uint64_t PHYS_MAGIC_SIG = 0xDEADBEEFE9000000ULL;

/* Write memory barrier for the endpoint APU (Cortex-A78). */
inline void ep_wmb()
{
#if defined(__aarch64__)
    __asm__ volatile("dsb st" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sfence" ::: "memory");
#else
    __sync_synchronize();
#endif
}

} // namespace

EpProxyBridge::EpProxyBridge()
    : mem_fd_(-1),
      bram_map_base_(nullptr), bram_map_len_(0),
      msix_map_base_(nullptr), msix_map_len_(0),
      bram_(nullptr), bram_header_(nullptr), msix_reg_(nullptr),
      num_channels_(0), stamp_seq_(false), ready_(false), finalized_(false)
{
    for (int c = 0; c < MDB5_MAX_CHANNELS; ++c) {
        slots_[c] = nullptr;
        frame_seq_[c] = 0;
        for (int b = 0; b < MAX_BUFFERS; ++b)
            phys_[c][b] = 0;
    }
}

EpProxyBridge::~EpProxyBridge()
{
    shutdown();
}

bool EpProxyBridge::init(int num_channels, bool stamp_seq)
{
    if (num_channels < 1 || num_channels > MDB5_MAX_CHANNELS) {
        fprintf(stderr, "EpProxyBridge: invalid num_channels %d\n", num_channels);
        return false;
    }
    num_channels_ = num_channels;
    stamp_seq_ = stamp_seq;

    mem_fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd_ < 0) {
        fprintf(stderr,
                "EpProxyBridge: open /dev/mem failed (need root): %s\n"
                "  -> PCIe frame sharing DISABLED; running local-only.\n",
                strerror(errno));
        return false;
    }

    /* Map BRAM mailbox. */
    uint64_t bram_phys = MDB5_EP_BRAM_PHYS;
    uint64_t bram_page = bram_phys & ~(EPB_PAGE_SIZE - 1);
    size_t   bram_off  = (size_t)(bram_phys - bram_page);
    bram_map_len_ = MDB5_BRAM_SIZE + bram_off;

    bram_map_base_ = mmap(NULL, bram_map_len_, PROT_READ | PROT_WRITE,
                          MAP_SHARED, mem_fd_, bram_page);
    if (bram_map_base_ == MAP_FAILED) {
        fprintf(stderr, "EpProxyBridge: mmap BRAM failed: %s\n", strerror(errno));
        bram_map_base_ = nullptr;
        close(mem_fd_);
        mem_fd_ = -1;
        return false;
    }
    bram_ = (volatile void *)((char *)bram_map_base_ + bram_off);
    bram_header_ = (volatile struct mdb5_mbox_header *)bram_;

    /* Map MSI-X trigger register. */
    uint64_t msix_phys = MDB5_EP_MSIX_TRIGGER_PHYS;
    uint64_t msix_page = msix_phys & ~(EPB_PAGE_SIZE - 1);
    size_t   msix_off  = (size_t)(msix_phys - msix_page);
    msix_map_len_ = EPB_PAGE_SIZE;

    msix_map_base_ = mmap(NULL, msix_map_len_, PROT_READ | PROT_WRITE,
                          MAP_SHARED, mem_fd_, msix_page);
    if (msix_map_base_ == MAP_FAILED) {
        fprintf(stderr, "EpProxyBridge: mmap MSI-X failed: %s\n", strerror(errno));
        msix_map_base_ = nullptr;
        munmap(bram_map_base_, bram_map_len_);
        bram_map_base_ = nullptr;
        bram_ = nullptr;
        bram_header_ = nullptr;
        close(mem_fd_);
        mem_fd_ = -1;
        return false;
    }
    msix_reg_ = (volatile uint32_t *)((char *)msix_map_base_ + msix_off);

    /* Clear the mailbox region (header + per-channel slots). */
    volatile uint32_t *p = (volatile uint32_t *)bram_;
    size_t words = (MDB5_MBOX_HEADER_SIZE +
                    (size_t)num_channels_ * MDB5_MBOX_SLOT_SIZE) / sizeof(uint32_t);
    for (size_t i = 0; i < words; ++i)
        p[i] = 0;
    ep_wmb();

    ready_ = true;
    printf("EpProxyBridge: BRAM at %p, MSI-X at %p, %d channels, stamp=%s\n",
           (void *)bram_, (void *)msix_reg_, num_channels_,
           stamp_seq_ ? "on" : "off");
    return true;
}

bool EpProxyBridge::registerChannel(int ch, uint32_t width, uint32_t height,
                                    uint32_t fourcc, uint32_t bytesperline,
                                    uint32_t sizeimage)
{
    if (!ready_ || ch < 0 || ch >= num_channels_)
        return false;

    volatile struct mdb5_mbox_slot *slot = (volatile struct mdb5_mbox_slot *)
        ((char *)bram_ + MDB5_MBOX_SLOT_OFFSET(ch));
    slots_[ch] = slot;

    slot->width        = width;
    slot->height       = height;
    slot->pixel_format = fourcc;
    slot->bytesperline = bytesperline;
    slot->frame_size   = sizeimage;
    slot->frame_status = MDB5_FRAME_STREAMING;
    ep_wmb();

    printf("EpProxyBridge: ch%d registered %ux%u fourcc=0x%08x stride=%u size=%u\n",
           ch, width, height, fourcc, bytesperline, sizeimage);
    return true;
}

uint64_t EpProxyBridge::discoverPhys(int ch, int buf_index, void *va, size_t len)
{
    if (!ready_ || ch < 0 || ch >= num_channels_ ||
        buf_index < 0 || buf_index >= MAX_BUFFERS || !va)
        return 0;

    if (phys_[ch][buf_index] != 0)
        return phys_[ch][buf_index]; /* cached */

    /* Write a unique magic pattern to this buffer. */
    struct phys_magic magic;
    magic.sig      = PHYS_MAGIC_SIG | (uint32_t)buf_index;
    magic.buf_index = (uint32_t)buf_index;
    magic.checksum = (uint32_t)buf_index ^ 0xCAFE;
    memcpy(va, &magic, sizeof(magic));
    __sync_synchronize();

    /* Scan the CMA region via /dev/mem for the matching pattern. */
    uint64_t found = 0;
    for (uint64_t off = 0; off < EPB_CMA_SCAN_SIZE && !found;
         off += EPB_CMA_SCAN_CHUNK) {
        void *m = mmap(NULL, EPB_CMA_SCAN_CHUNK, PROT_READ, MAP_SHARED,
                       mem_fd_, EPB_CMA_BASE + off);
        if (m == MAP_FAILED)
            continue;

        for (uint64_t pg = 0; pg < EPB_CMA_SCAN_CHUNK; pg += EPB_PAGE_SIZE) {
            struct phys_magic *cand = (struct phys_magic *)((char *)m + pg);
            if (cand->sig == magic.sig &&
                cand->buf_index == magic.buf_index &&
                cand->checksum == magic.checksum) {
                found = EPB_CMA_BASE + off + pg;
                break;
            }
        }
        munmap(m, EPB_CMA_SCAN_CHUNK);
    }

    /* Clear the magic so it does not linger in the frame buffer. */
    memset(va, 0, sizeof(magic));

    if (!found) {
        fprintf(stderr, "EpProxyBridge: ch%d buf%d phys discovery FAILED\n",
                ch, buf_index);
        return 0;
    }

    phys_[ch][buf_index] = found;
    printf("EpProxyBridge: ch%d buf%d va=%p pa=0x%llx len=%zu\n",
           ch, buf_index, va, (unsigned long long)found, len);
    return found;
}

void EpProxyBridge::publishFrame(int ch, int buf_index, void *va, uint32_t size)
{
    if (!ready_ || ch < 0 || ch >= num_channels_ || !slots_[ch])
        return;
    if (buf_index < 0 || buf_index >= MAX_BUFFERS)
        return;

    uint64_t phys = phys_[ch][buf_index];
    if (phys == 0)
        return; /* physical address unknown -> cannot share this buffer */

    volatile struct mdb5_mbox_slot *slot = slots_[ch];
    uint32_t seq = ++frame_seq_[ch];

    /* Optional in-band watermark (overwrites the first bytes / top-left). */
    if (stamp_seq_ && va) {
        struct timespec wts;
        clock_gettime(CLOCK_MONOTONIC, &wts);

        struct mdb5_frame_wm wm;
        wm.magic            = MDB5_FRAME_WM_MAGIC;
        wm.ch_index         = (uint32_t)ch;
        wm.frame_seq        = seq;
        wm.reserved         = 0;
        wm.capture_ktime_ns = (uint64_t)wts.tv_sec * 1000000000ULL +
                              (uint64_t)wts.tv_nsec;
        memcpy(va, &wm, sizeof(wm));
    }

    /* Write frame metadata to the BRAM mailbox slot. */
    slot->frame_addr_lo  = (uint32_t)(phys & 0xFFFFFFFF);
    slot->frame_addr_hi  = (uint32_t)(phys >> 32);
    slot->frame_size     = size;
    slot->frame_sequence = seq;

    /* Barrier before raising VALID so metadata + pixels reach DDR first. */
    ep_wmb();
    slot->frame_status = MDB5_FRAME_VALID | MDB5_FRAME_STREAMING;

    /* Fire MSI-X to the host (offset past the eDMA vectors). Non-blocking:
     * we do NOT wait for the host ACK, so the display loop is not stalled. */
    *msix_reg_ = MDB5_EDMA_IRQS + ch;
}

void EpProxyBridge::finalize()
{
    if (!ready_ || finalized_)
        return;

    bram_header_->num_channels = num_channels_;
    bram_header_->ep_status    = MDB5_EP_STATUS_READY;
    ep_wmb();
    bram_header_->magic = MDB5_MAGIC; /* host polls for this last */
    finalized_ = true;

    printf("EpProxyBridge: mailbox live, magic=0x%08x, %d channels\n",
           MDB5_MAGIC, num_channels_);
}

void EpProxyBridge::shutdown()
{
    if (bram_header_) {
        /* Tell the host the endpoint is going away. */
        bram_header_->magic = 0;
        ep_wmb();
    }
    if (msix_map_base_) {
        munmap(msix_map_base_, msix_map_len_);
        msix_map_base_ = nullptr;
    }
    if (bram_map_base_) {
        munmap(bram_map_base_, bram_map_len_);
        bram_map_base_ = nullptr;
    }
    if (mem_fd_ >= 0) {
        close(mem_fd_);
        mem_fd_ = -1;
    }
    bram_ = nullptr;
    bram_header_ = nullptr;
    msix_reg_ = nullptr;
    ready_ = false;
    finalized_ = false;
}
