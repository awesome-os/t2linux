# T2Linux

**Sovereign cross-compiled bootable hybrid UEFI/BIOS ISO and QEMU QCOW2 (`x86_64` + `aarch64`) powered by Awesome-OS `b8g-ghost` CAS.**

T2Linux boots offline straight into a zero-dependency static musl C11 PID 1 (`/init`) verified against `B8G_ABI_MAGIC_V3` (`0x33564742`), mounts a persistent `LABEL=B8G_CAS` `ext4` volume (or `tmpfs` on live ISO), and hydrates everything on demand from `awesome-os/b8g-ghost @ cas-v1`.

## Architecture

```text
+-------------------------------------------------------------------+
|  Limine v8 Hybrid Bootloader (UEFI BOOTX64.EFI + Legacy BIOS)     |
+-------------------------------------------------------------------+
                                 |
                                 v
+-------------------------------------------------------------------+
|  Linux Kernel (vmlinuz) + zstd initramfs (initramfs.cpio.zst)     |
+-------------------------------------------------------------------+
                                 |
                                 v
+-------------------------------------------------------------------+
|  /init (Static musl C11 PID 1 — x86_64 & aarch64)                 |
|  - Mounts /proc, /sys, /dev (devtmpfs), /tmp, /run                |
|  - Mounts /var/lib/b8g/cas (ext4 LABEL=B8G_CAS or tmpfs fallback) |
|  - Verifies B8G_ABI_MAGIC_V3 (0x33564742)                         |
|  - Handles t2.selftest=1 (CI self-test + clean ACPI poweroff)     |
|  - Brings up lo + non-blocking DHCP on eth0                       |
+-------------------------------------------------------------------+
                                 |
                                 v
+-------------------------------------------------------------------+
|  /bin/b8g-ghost (CAS hydrator from awesome-os/b8g-ghost@cas-v1)   |
|  + /bin/busybox (sh, mount, uname, ip, udhcpc, ls, cat, poweroff) |
+-------------------------------------------------------------------+
```

## Quick Start

Download the latest release artifacts from [GitHub Releases](https://github.com/awesome-os/t2linux/releases):

- `t2linux-x86_64.iso` — Hybrid UEFI + Legacy BIOS bootable ISO
- `t2linux-x86_64.qcow2` — GPT disk image (FAT32 ESP + persistent `ext4` `B8G_CAS` partition)
- `vmlinuz-x86_64` & `initramfs-x86_64.cpio.zst` — Direct QEMU kernel/initrd boot artifacts

### 1. Boot in QEMU (`x86_64`)

**Windows 11 (PowerShell):**

```powershell
# Boot hybrid ISO in serial console
.\scripts\run-qemu.ps1 -Image .\dist\t2linux-x86_64.iso

# Boot QCOW2 with persistent B8G_CAS partition
.\scripts\run-qemu.ps1 -Image .\dist\t2linux-x86_64.qcow2 -Memory 2048

# Direct kernel + initramfs CI self-test
.\scripts\run-qemu.ps1 -Kernel .\dist\vmlinuz-x86_64 -Initrd .\dist\initramfs-x86_64.cpio.zst -SelfTest
```

**Linux / macOS:**

```bash
# Hybrid ISO (BIOS)
qemu-system-x86_64 -cdrom t2linux-x86_64.iso -m 1024 -nographic

# Hybrid ISO (UEFI / OVMF)
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom t2linux-x86_64.iso -m 1024 -nographic

# QCOW2 disk image
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -drive file=t2linux-x86_64.qcow2,format=qcow2,if=virtio -m 1024 -nographic

# Direct kernel + initramfs self-test
qemu-system-x86_64 \
  -kernel vmlinuz-x86_64 \
  -initrd initramfs-x86_64.cpio.zst \
  -append "console=ttyS0,115200 t2.selftest=1" \
  -nographic -m 512 -no-reboot
```

### 2. Boot in QEMU (`aarch64`)

```bash
qemu-system-aarch64 \
  -M virt -cpu cortex-a72 -m 1024 -nographic \
  -kernel vmlinuz-aarch64 \
  -initrd initramfs-aarch64.cpio.zst \
  -append "console=ttyAMA0,115200"
```

### 3. Proxmox VE (`qm importdisk`)

```bash
# Create VM 9000 with OVMF (UEFI) and serial console
qm create 9000 --name t2linux --memory 1024 --cores 2 --net0 virtio,bridge=vmbr0 \
  --bios ovmf --machine q35 --serial0 socket --vga serial0

# Import QCOW2 into local-lvm
qm importdisk 9000 t2linux-x86_64.qcow2 local-lvm

# Attach disk and set boot order
qm set 9000 --scsihw virtio-scsi-pci --scsi0 local-lvm:vm-9000-disk-0
qm set 9000 --boot order=scsi0
qm start 9000
qm terminal 9000
```

### 4. Bare-Metal UEFI / BIOS USB

**Linux / macOS (`dd`):**

```bash
sudo dd if=t2linux-x86_64.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

**Windows (Rufus):**

1. Select `t2linux-x86_64.iso`.
2. Choose **DD Image mode** when prompted.
3. Boot on any UEFI or Legacy BIOS `x86_64` machine.

## Cross-Compiling `/init` Locally

Using Zig (`zig cc`) on Windows 11, Linux, or macOS:

```powershell
# x86_64 static musl
zig cc -target x86_64-linux-musl -static -Os -s -Wall -Wextra src/init.c -o dist/init-x86_64

# aarch64 static musl
zig cc -target aarch64-linux-musl -static -Os -s -Wall -Wextra src/init.c -o dist/init-aarch64
```

## License

MIT — see [LICENSE](LICENSE).
