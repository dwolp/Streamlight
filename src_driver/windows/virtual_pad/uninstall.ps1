# Removes the Streamlight virtual Switch Pro driver. Run from an elevated PowerShell.

#Requires -RunAsAdministrator

$ErrorActionPreference = "Continue"

$wdkTools = "${env:ProgramFiles(x86)}\Windows Kits\10\Tools\10.0.26100.0\x64"
$devcon = Join-Path $wdkTools "devcon.exe"
$hardwareId = "Root\StreamlightVirtualPad"

# Remove the device node first, then the driver package.
& $devcon remove $hardwareId

# Find the published package name (oemNN.inf) for our driver and delete it.
# pnputil lists "Published Name" before "Original Name" within each entry.
$published = $null
foreach ($line in (pnputil /enum-drivers)) {
    if ($line -match "Published Name\s*:\s*(oem\d+\.inf)") {
        $published = $Matches[1]
    }
    if ($line -match "Original Name\s*:\s*streamlight_virtual_pad\.inf" -and $published) {
        pnputil /delete-driver $published /uninstall /force
        break
    }
}
Write-Host "Done."
