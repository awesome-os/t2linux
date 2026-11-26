<#
.SYNOPSIS
    Boot Awesome-OS Linux v11 Hybrid UEFI ISO directly in Windows 11 Hyper-V (Gen-2 UEFI VM).
.EXAMPLE
    .\scripts\run-hyperv.ps1
.EXAMPLE
    .\scripts\run-hyperv.ps1 -IsoPath .\dist\t2linux-x86_64.iso -MemoryMB 2048 -Cpus 4
#>
[CmdletBinding()]
param(
    [string]$VmName = "AwesomeOS-BareMetal",
    [string]$IsoPath = "",
    [int]$MemoryMB = 2048,
    [int]$Cpus = 2,
    [switch]$Recreate
)

$ErrorActionPreference = "Stop"

if (-not $IsoPath) {
    $IsoPath = Join-Path (Split-Path -Parent $PSScriptRoot) "dist\t2linux-x86_64.iso"
}
$IsoPath = (Resolve-Path $IsoPath).Path

$existing = Get-VM -Name $VmName -ErrorAction SilentlyContinue
if ($existing -and $Recreate) {
    if ($existing.State -ne 'Off') { Stop-VM -Name $VmName -TurnOff -Force }
    Remove-VM -Name $VmName -Force
    $existing = $null
}

if (-not $existing) {
    Write-Host "[AWESOME-OS] Creating Gen-2 UEFI Hyper-V VM '$VmName' ($MemoryMB MB RAM, $Cpus vCPU)..."
    New-VM -Name $VmName -MemoryStartupBytes ($MemoryMB * 1MB) -Generation 2 -SwitchName "Default Switch" | Out-Null
    Set-VMProcessor -VMName $VmName -Count $Cpus
    Set-VMFirmware -VMName $VmName -EnableSecureBoot Off
    $dvd = Add-VMDvdDrive -VMName $VmName -Path $IsoPath -Passthru
    Set-VMFirmware -VMName $VmName -FirstBootDevice $dvd
} else {
    $dvd = Get-VMDvdDrive -VMName $VmName | Select-Object -First 1
    if ($dvd) {
        Set-VMDvdDrive -VMName $VmName -ControllerNumber $dvd.ControllerNumber -ControllerLocation $dvd.ControllerLocation -Path $IsoPath
    } else {
        $dvd = Add-VMDvdDrive -VMName $VmName -Path $IsoPath -Passthru
    }
    Set-VMFirmware -VMName $VmName -EnableSecureBoot Off -FirstBootDevice $dvd
}

$vm = Get-VM -Name $VmName
if ($vm.State -ne 'Running') {
    Write-Host "[AWESOME-OS] Starting Hyper-V VM '$VmName' -> $IsoPath"
    Start-VM -Name $VmName
}
Start-Process vmconnect.exe -ArgumentList "localhost", "`"$VmName`""
Write-Host "[AWESOME-OS] Hyper-V UEFI console opened for '$VmName'."
