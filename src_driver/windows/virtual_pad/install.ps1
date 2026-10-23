# Installs the Streamlight virtual Switch Pro driver on this PC for testing.
# Run from an elevated PowerShell after build.cmd has succeeded.
# Requires test-signing to be enabled (bcdedit /set testsigning on, then reboot).

#Requires -RunAsAdministrator

$ErrorActionPreference = "Stop"

$here = $PSScriptRoot
$wdkBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0"
$wdkTools = "${env:ProgramFiles(x86)}\Windows Kits\10\Tools\10.0.26100.0\x64"
$inf2cat = Join-Path $wdkBin "x86\Inf2Cat.exe"
$signtool = Join-Path $wdkBin "x64\signtool.exe"
$devcon = Join-Path $wdkTools "devcon.exe"
$certName = "Streamlight Test Driver"
$hardwareId = "Root\StreamlightVirtualPad"

$sys = Join-Path $here "build\streamlight_virtual_pad.sys"
$inf = Join-Path $here "streamlight_virtual_pad.inf"
$pkg = Join-Path $here "build\pkg"

foreach ($path in @($sys, $inf, $inf2cat, $signtool, $devcon)) {
    if (-not (Test-Path $path)) { throw "Missing: $path" }
}

# 1. Stage the driver package.
Remove-Item $pkg -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $pkg | Out-Null
Copy-Item $sys, $inf $pkg

# 2. Find or create the self-signed test certificate, and trust it on this machine.
$cert = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq "CN=$certName" } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=$certName" `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(5)
}
$cerPath = Join-Path $pkg "streamlight-test.cer"
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
certutil -addstore -f Root $cerPath | Out-Null
certutil -addstore -f TrustedPublisher $cerPath | Out-Null

# 3. Create the catalog and sign the driver and catalog.
& $inf2cat /driver:$pkg /os:10_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed" }

# /sm /s My: the certificate is in the machine store; signtool searches the user store by default.
& $signtool sign /fd sha256 /sm /s My /sha1 $cert.Thumbprint (Join-Path $pkg "streamlight_virtual_pad.sys")
if ($LASTEXITCODE -ne 0) { throw "signtool failed for the driver" }
& $signtool sign /fd sha256 /sm /s My /sha1 $cert.Thumbprint (Join-Path $pkg "streamlight_virtual_pad.cat")
if ($LASTEXITCODE -ne 0) { throw "signtool failed for the catalog" }

# 4. Add the driver package to the driver store. /install also updates an existing controller.
pnputil /add-driver (Join-Path $pkg "streamlight_virtual_pad.inf") /install
# pnputil: 0 = success, 3010 = success but a restart is needed (the old driver was in use).
$rebootNeeded = $LASTEXITCODE -eq 3010
if ($LASTEXITCODE -ne 0 -and -not $rebootNeeded) { throw "pnputil failed with exit code $LASTEXITCODE" }

# 5. Create the root device on first install. If it already exists, pnputil has updated it;
# running "devcon install" again would add a second virtual controller.
$existing = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.HardwareID -contains $hardwareId }
if (-not $existing) {
    & $devcon install (Join-Path $pkg "streamlight_virtual_pad.inf") $hardwareId
    # devcon: 0 = success, 1 = success but a restart is needed, anything else = failure.
    if ($LASTEXITCODE -eq 1) { $rebootNeeded = $true }
    elseif ($LASTEXITCODE -ne 0) { throw "devcon failed with exit code $LASTEXITCODE" }
}

if ($rebootNeeded) {
    Write-Host "Installed. Restart Windows to load the new driver."
} else {
    Write-Host "Installed. Open joy.cpl (Set up USB game controllers) to check the device."
}
