# Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
# SPDX-License-Identifier: MIT
# Contributors: BW
# Maintainer: BW
# Makefile for MDB5 endpoint proxy
#
# Native build:   make
# Cross-compile:  make CROSS_COMPILE=aarch64-linux-gnu-

CC ?= gcc
CROSS_COMPILE ?=

CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -pthread

TARGET = ep_proxy

.PHONY: all clean

all: $(TARGET)

$(TARGET): ep_proxy.c ep_proxy.h mdb5_bridge.h
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ ep_proxy.c $(LDFLAGS) -lpthread

clean:
	rm -f $(TARGET)
