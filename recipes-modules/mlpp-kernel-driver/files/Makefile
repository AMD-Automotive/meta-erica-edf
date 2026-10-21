# Copyright (C) 2020-2022, Xilinx, Inc.
# Copyright (C) 2022-2025, Advanced Micro Devices, Inc.
# SPDX-License-Identifier: GPL-2.0-only
# KERNELRELEASE	?= `uname -r`
# KERNEL_DIR	?= /lib/modules/$(KERNELRELEASE)/build
# PWD		:= $(shell pwd)
# EXTRA_CFLAGS := -I$(src) 
# MODNAME	:= ml_pre_process
# ARCH=arm64
# CROSS_COMPILE=aarch64-amd-linux-

# Define a single module with multiple source files
SRC := $(shell pwd)
obj-m := ml_pre_process.o
ml_pre_process-y := ml_pre_process_main.o vpss_helpers.o mlpp_ffc_in.o mlpp_svc_in.o mlpp_outputs.o

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE} -C ${KERNEL_SRC} M=$(SRC) clean
