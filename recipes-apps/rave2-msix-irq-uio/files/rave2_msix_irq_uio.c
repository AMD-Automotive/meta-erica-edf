// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: GPL-2.0-only
// Contributors: BW
// Maintainer: BW
//
// rave2_msix_irq_uio.c
//
// Userspace daemon: waits on /dev/uio0..3 (one per buf_manager channel,
// bound via uio_pdrv_genirq + generic-uio DT nodes) and writes the
// binary-encoded channel index to the MMI PCIe MSI-X trigger register
// at 0xED800948 to generate an MSI-X to the PCIe host.
//
// Build on target:  gcc -O2 -o rave2_msix_irq_uio rave2_msix_irq_uio.c
// Run:              ./rave2_msix_irq_uio [uio_base_index]
//                   e.g. ./rave2_msix_irq_uio 0   (uses /dev/uio0..3)
//
// DT overlay nodes required (one per channel, matched by uio_pdrv_genirq):
//
//   ch0: compatible = "generic-uio"; interrupt-parent = <&gic>;
//        interrupts = <0 CH0_SPI 1>;  /* IRQ_TYPE_EDGE_RISING */
//   ch1..3: same pattern with CH1..3_SPI numbers

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_CHANNELS        4
#define MSIX_GEN_PHYS       0xED800948UL
#define PAGE_SIZE           4096UL
#define PAGE_MASK           (~(PAGE_SIZE - 1))

static volatile int running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[])
{
    int uio_base = 0;
    if (argc > 1)
        uio_base = atoi(argv[1]);

    /* Map the MSI-X trigger register via /dev/mem */
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    off_t page_base = MSIX_GEN_PHYS & PAGE_MASK;
    off_t page_off  = MSIX_GEN_PHYS - page_base;

    void *map = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, mem_fd, page_base);
    if (map == MAP_FAILED) {
        perror("mmap /dev/mem");
        close(mem_fd);
        return 1;
    }

    volatile uint32_t *msix_reg = (volatile uint32_t *)((char *)map + page_off);
    printf("Mapped MSI-X register at 0x%lx\n", MSIX_GEN_PHYS);

    /* Open /dev/uioN for each channel */
    struct pollfd pfds[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/uio%d", uio_base + i);
        pfds[i].fd = open(path, O_RDWR);
        if (pfds[i].fd < 0) {
            fprintf(stderr, "open %s: %s\n", path, strerror(errno));
            fprintf(stderr, "Is the new bitstream loaded and DT overlay applied?\n");
            munmap(map, PAGE_SIZE);
            close(mem_fd);
            return 1;
        }
        pfds[i].events = POLLIN;

        /* Re-arm: write 1 to enable interrupts on this UIO device */
        uint32_t enable = 1;
        write(pfds[i].fd, &enable, sizeof(enable));

        printf("Channel %d: %s fd=%d\n", i, path, pfds[i].fd);
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("Waiting for buf_manager interrupts...\n");

    while (running) {
        int ret = poll(pfds, NUM_CHANNELS, 500 /* ms timeout for signal check */);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (!(pfds[i].revents & POLLIN))
                continue;

            /* Drain the UIO interrupt count */
            uint32_t count;
            if (read(pfds[i].fd, &count, sizeof(count)) < 0) {
                perror("read uio");
                continue;
            }

            /* Write binary channel index to trigger MSI-X */
            *msix_reg = (uint32_t)i;

            /* Re-arm interrupt */
            uint32_t enable = 1;
            write(pfds[i].fd, &enable, sizeof(enable));

            printf("ch%d: MSI-X triggered (irq_count=%u)\n", i, count);
        }
    }

    printf("Shutting down\n");
    for (int i = 0; i < NUM_CHANNELS; i++)
        close(pfds[i].fd);
    munmap(map, PAGE_SIZE);
    close(mem_fd);
    return 0;
}
