# Checks that the driver stops a rumble the host no longer refreshes, as the real controller does.
# Hosts such as SDL keep a rumble going by resending it and may end it by simply stopping.
#
# Start the control tool first; it prints the rumble the driver passes on:
#   build\tests\pad_control.exe 10
# Then, from another shell:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_rumble_timeout.ps1
# pad_control should print a full low-frequency rumble and, about 200 ms later, rumble 0.

$ErrorActionPreference = "Stop"

Add-Type -Path (Join-Path $PSScriptRoot "SwitchPad.cs")

$path = [SwitchPad]::FindPath(0x057E, 0x2009)
if (-not $path) { Write-Host "No virtual controller found. Is pad_control.exe running?"; exit 1 }
$stream = [SwitchPad]::Open($path)

try {
    # One rumble-only report (0x10): packet counter, left actuator with the low band at full
    # strength (00 01 40 72), right actuator neutral (00 01 40 40). No refresh follows.
    $packet = New-Object byte[] 64
    [byte[]] $rumble = 0x10, 0x01, 0x00, 0x01, 0x40, 0x72, 0x00, 0x01, 0x40, 0x40
    [Array]::Copy($rumble, $packet, $rumble.Length)
    $stream.Write($packet, 0, 64)
    $stream.Flush()
    Write-Host "Sent one rumble report and stopped refreshing it."
    Start-Sleep -Milliseconds 1000
} finally {
    $stream.Close()
}
