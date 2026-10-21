echo "Setting up reserved memory node for Linux"

setenv scratch_base 0x20000000
setenv scratch_size 0x10000000   # 256 MiB
setenv scratch_node scratchpad@20000000

# Kernel cmdline for the recovery image. PID-1 init bypasses systemd; quiet
# but not silent (loglevel=3) so kernel errors remain visible. panic=5 forces
# auto-reboot if PID 1 dies. console matches RAVE2 baseline (PL011 ttyAMA0).
#
# Use rdinit= (not init=): the recovery image boots from an initramfs/cpio
# rootfs, and the kernel always execs /init from there unless overridden by
# rdinit=. (init= is only honored after a real-rootfs switch, which we never
# do.) Path is /usr/sbin/recovery-init under usrmerge.
setenv bootargs "earlycon=pl011,mmio32,0xf1920000,115200n8 console=ttyAMA0,115200n8 loglevel=3 rdinit=/usr/sbin/recovery-init panic=5 ip=off"

fdt addr ${fdtcontroladdr}
fdt resize 0x1000

# Create /reserved-memory if needed
if fdt print /reserved-memory; then
	true
else
	fdt mknode / reserved-memory
fi

fdt set /reserved-memory '#address-cells' <2>
fdt set /reserved-memory '#size-cells' <2>
fdt set /reserved-memory ranges

# Create the reservation node if needed
if fdt print /reserved-memory/${scratch_node}; then
	true
else
	fdt mknode /reserved-memory ${scratch_node}
fi

fdt set /reserved-memory/${scratch_node} reg <0x0 ${scratch_base} 0x0 ${scratch_size}>
fdt set /reserved-memory/${scratch_node} compatible "shared-dma-pool"

echo "Booting kernel with embedded ramdisk and boot.bin device tree"
bootm 0x20005000:kernel 0x20005000:ramdisk ${fdtcontroladdr}

echo "Boot failed, dropping to U-Boot prompt"