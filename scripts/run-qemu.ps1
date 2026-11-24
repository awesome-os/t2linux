<#
.SYNOPSIS
    Boot T2Linux ISO or QCOW2 image in QEMU on Windows 11.
.EXAMPLE
    .\scripts\run-qemu.ps1 -Image .\dist\t2linux-x86_64.iso
.EXAMPLE
    .\scripts\run-qemu.ps1 -Image .\dist\t2linux-x86_64.qcow2 -Memory 2048 -Gui
.EXAMPLE
    .\scripts\run-qemu.ps1 -Kernel .\dist\vmlinuz-x86_64 -Initrd .\dist\initramfs-x86_64.cpio.zst -SelfTest
#>
[CmdletBinding()]
param(
    [string]$Image = "",
    [string]$Kernel = "",
    [string]$Initrd = "",
    [ValidateSet("x86_64", "aarch64")]
    [string]$Arch = "x86_64",
    [int]$Memory = 1024,
    [int]$Cpus = 2,
    [switch]$Gui,
    [switch]$SelfTest,
    [string]$Ovmf = ""
)

$ErrorActionPreference = "Stop"

$qemuBin = if ($Arch -eq "aarch64") { "qemu-system-aarch64" } else { "qemu-system-x86_64" }
if (-not (Get-Command $qemuBin -ErrorAction SilentlyContinue)) {
    throw "Could not find '$qemuBin' in PATH. Install QEMU (winget install SoftwareFreedomConservancy.QEMU)."
}

$argsList = @(
    "-m", "$Memory",
    "-smp", "$Cpus",
    "-netdev", "user,id=net0",
    "-device", "virtio-net-pci,netdev=net0"
)

if ($Arch -eq "aarch64") {
    $argsList += @("-M", "virt", "-cpu", "cortex-a72")
} else {
    $argsList += @("-cpu", "max")
}

if (-not $Gui) {
    $argsList += @("-nographic")
}

if ($Ovmf -and (Test-Path $Ovmf)) {
    $argsList += @("-bios", $Ovmf)
}

if ($Kernel -and $Initrd) {
    $cmdline = if ($SelfTest) { "console=ttyS0,115200 quiet t2.selftest=1" } else { "console=ttyS0,115200" }
    $argsList += @("-kernel", $Kernel, "-initrd", $Initrd, "-append", $cmdline)
    if ($SelfTest) {
        $argsList += @("-no-reboot")
    }
} elseif ($Image) {
    if (-not (Test-Path $Image)) {
        throw "Image not found: $Image"
    }
    $ext = [System.IO.Path]::GetExtension($Image).ToLowerInvariant()
    if ($ext -eq ".iso") {
        $argsList += @("-cdrom", $Image, "-boot", "d")
    } elseif ($ext -eq ".qcow2") {
        $argsList += @("-drive", "file=$Image,format=qcow2,if=virtio")
    } else {
        $argsList += @("-drive", "file=$Image,format=raw,if=virtio")
    }
} else {
    $defaultIso = Join-Path $PSScriptRoot "..\dist\t2linux-$Arch.iso"
    $defaultQcow2 = Join-Path $PSScriptRoot "..\dist\t2linux-$Arch.qcow2"
    if (Test-Path $defaultIso) {
        $argsList += @("-cdrom", $defaultIso, "-boot", "d")
    } elseif (Test-Path $defaultQcow2) {
        $argsList += @("-drive", "file=$defaultQcow2,format=qcow2,if=virtio")
    } else {
        throw "No -Image or -Kernel/-Initrd specified and no default image found in dist/."
    }
}

Write-Host "[T2LINUX] Launching: $qemuBin $($argsList -join ' ')"
& $qemuBin @argsList
exit $LASTEXITCODE
