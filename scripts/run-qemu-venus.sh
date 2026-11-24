#!/usr/bin/env sh
# Optional T2Linux QEMU launcher with VirtIO-GPU Venus (Vulkan / OpenGL ES VM GPU acceleration)
# Requires host QEMU with virglrenderer >= 1.0 (venus enabled) or libkrun / krunkit / atomickrisp.
#
# Usage:
#   sh scripts/run-qemu-venus.sh [path/to/t2linux-x86_64.qcow2]
set -eu

IMG="${1:-t2linux-x86_64.qcow2}"
MEM="${T2_VM_MEM:-4G}"
CPUS="${T2_VM_CPUS:-4}"
HOSTMEM="${T2_VENUS_HOSTMEM:-4G}"

if [ ! -f "$IMG" ]; then
  echo "[t2linux-venus] Disk image not found: $IMG" >&2
  echo "Download from: https://github.com/awesome-os/t2linux/releases/latest" >&2
  exit 1
fi

echo "[t2linux-venus] Booting $IMG with VirtIO-GPU Venus (hostmem=$HOSTMEM, blob=true, venus=true)..."
exec qemu-system-x86_64 \
  -enable-kvm \
  -machine q35 \
  -cpu host \
  -smp "$CPUS" \
  -m "$MEM" \
  -object memory-backend-memfd,id=mem1,size="$MEM" \
  -machine memory-backend=mem1 \
  -device virtio-gpu-gl,hostmem="$HOSTMEM",blob=true,venus=true \
  -display gtk,gl=on \
  -drive file="$IMG",if=virtio,format=qcow2 \
  -netdev user,id=net0 \
  -device virtio-net-pci,netdev=net0
