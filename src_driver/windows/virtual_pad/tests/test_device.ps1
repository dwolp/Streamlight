# Runs SDL's Switch Pro Controller start-up sequence against the installed virtual controller
# and checks every reply. Only reads and writes HID reports, so no admin rights are needed.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_device.ps1

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class SwitchPad {
  [DllImport("hid.dll")] static extern void HidD_GetHidGuid(out Guid g);
  [DllImport("hid.dll")] static extern bool HidD_GetAttributes(SafeFileHandle h, ref HIDD_ATTRIBUTES a);
  [DllImport("setupapi.dll", CharSet = CharSet.Auto)] static extern IntPtr SetupDiGetClassDevs(ref Guid g, IntPtr e, IntPtr p, int f);
  [DllImport("setupapi.dll")] static extern bool SetupDiEnumDeviceInterfaces(IntPtr s, IntPtr d, ref Guid g, int i, ref SP_DEVICE_INTERFACE_DATA data);
  [DllImport("setupapi.dll", CharSet = CharSet.Auto)] static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref SP_DEVICE_INTERFACE_DATA d, IntPtr detail, int size, out int req, IntPtr info);
  [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
  [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
  static extern SafeFileHandle CreateFile(string n, uint a, uint s, IntPtr sa, uint c, uint f, IntPtr t);

  [StructLayout(LayoutKind.Sequential)] struct SP_DEVICE_INTERFACE_DATA { public int cbSize; public Guid g; public int flags; public IntPtr r; }
  [StructLayout(LayoutKind.Sequential)] struct HIDD_ATTRIBUTES { public int Size; public ushort Vid; public ushort Pid; public ushort Ver; }

  public static string FindPath(ushort vid, ushort pid) {
    Guid hid; HidD_GetHidGuid(out hid);
    IntPtr set = SetupDiGetClassDevs(ref hid, IntPtr.Zero, IntPtr.Zero, 0x12); // PRESENT | DEVICEINTERFACE
    try {
      for (int i = 0; ; i++) {
        var d = new SP_DEVICE_INTERFACE_DATA(); d.cbSize = Marshal.SizeOf(d);
        if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hid, i, ref d)) return null;
        int req; SetupDiGetDeviceInterfaceDetail(set, ref d, IntPtr.Zero, 0, out req, IntPtr.Zero);
        IntPtr buf = Marshal.AllocHGlobal(req);
        try {
          Marshal.WriteInt32(buf, IntPtr.Size == 8 ? 8 : 6);
          if (!SetupDiGetDeviceInterfaceDetail(set, ref d, buf, req, out req, IntPtr.Zero)) continue;
          string path = Marshal.PtrToStringAuto(new IntPtr(buf.ToInt64() + 4));
          using (var h = CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (h.IsInvalid) continue;
            var a = new HIDD_ATTRIBUTES(); a.Size = Marshal.SizeOf(a);
            if (HidD_GetAttributes(h, ref a) && a.Vid == vid && a.Pid == pid) return path;
          }
        } finally { Marshal.FreeHGlobal(buf); }
      }
    } finally { SetupDiDestroyDeviceInfoList(set); }
  }

  public static FileStream Open(string path) {
    var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero); // GENERIC_READ | GENERIC_WRITE
    if (h.IsInvalid) throw new IOException("open failed: " + Marshal.GetLastWin32Error());
    return new FileStream(h, FileAccess.ReadWrite, 64, false);
  }
}
"@

$script:failures = 0
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  PASS $what" } else { Write-Host "  FAIL $what"; $script:failures++ }
}

# Reads reports until one matches, or gives up after the timeout (SDL waits about 100 ms;
# allow more here because Windows timer resolution can stretch the driver's 8 ms interval).
function Read-Until([scriptblock]$match, [int]$timeoutMs = 500) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    $buf = New-Object byte[] 64
    while ((Get-Date) -lt $deadline) {
        $n = $stream.Read($buf, 0, 64)
        if ($n -gt 0 -and (& $match $buf)) { return , $buf }
    }
    return $null
}

function Send([byte[]]$bytes) {
    $packet = New-Object byte[] 64
    [Array]::Copy($bytes, $packet, $bytes.Length)
    $stream.Write($packet, 0, 64)
    $stream.Flush()
}

$script:counter = 0
function Send-Subcommand([byte]$id, [byte[]]$arguments = @()) {
    $script:counter = ($script:counter + 1) % 16
    $packet = [byte[]](0x01, $script:counter, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, $id) + $arguments
    Send $packet
    return Read-Until { param($r) $r[0] -eq 0x21 -and $r[14] -eq $id }
}

function Read-Spi([int]$address, [byte]$length) {
    $arguments = [byte[]](($address -band 0xFF), (($address -shr 8) -band 0xFF), (($address -shr 16) -band 0xFF), (($address -shr 24) -band 0xFF), $length)
    $reply = Send-Subcommand 0x10 $arguments
    if (-not $reply) { return $null }
    for ($i = 0; $i -lt 5; $i++) { if ($reply[15 + $i] -ne $arguments[$i]) { return $null } }
    return , $reply[20..(19 + $length)]
}

$path = [SwitchPad]::FindPath(0x057E, 0x2009)
if (-not $path) { Write-Host "No HID device with VID 057E PID 2009 found."; exit 1 }
Write-Host "Device: $path"
$stream = [SwitchPad]::Open($path)

try {
    Write-Host "USB commands"
    Send ([byte[]](0x80, 0x01))
    $r = Read-Until { param($r) $r[0] -eq 0x81 -and $r[1] -eq 0x01 }
    Check ($null -ne $r) "status (0x80 0x01) replied"
    if ($r) { Check ($r[3] -eq 0x03) "controller type is Pro (3)" }

    foreach ($command in 0x02, 0x03, 0x02) {
        Send ([byte[]](0x80, $command))
        $r = Read-Until { param($r) $r[0] -eq 0x81 -and $r[1] -eq $command }
        Check ($null -ne $r) ("0x80 0x{0:X2} replied" -f $command)
    }
    Send ([byte[]](0x80, 0x04))  # force USB: no reply expected

    Write-Host "SPI flash"
    $data = Read-Spi 0x8010 0x16
    Check ($null -ne $data -and $data[0] -eq 0xFF -and $data[1] -eq 0xFF) "user stick calibration absent (0x8010)"

    $data = Read-Spi 0x603D 0x12
    Check ($null -ne $data) "factory stick calibration read (0x603D)"
    if ($data) {
        # PowerShell keeps [byte] when shifting, which overflows; decode as int.
        $d = [int[]]$data
        $lxCenter = (($d[4] -shl 8) -band 0xF00) -bor $d[3]
        $lyCenter = ($d[5] -shl 4) -bor ($d[4] -shr 4)
        $rxCenter = (($d[10] -shl 8) -band 0xF00) -bor $d[9]
        $lxMax = (($d[1] -shl 8) -band 0xF00) -bor $d[0]
        Check ($lxCenter -eq 2048 -and $lyCenter -eq 2048 -and $rxCenter -eq 2048) "stick centres are 2048"
        Check ($lxMax -eq 1536) "left stick range is 1536"
    }

    $data = Read-Spi 0x6020 0x18
    Check ($null -ne $data -and $data[6] -eq 0x00 -and $data[7] -eq 0x40) "factory IMU calibration (0x6020)"

    $data = Read-Spi 0x8026 0x14
    Check ($null -ne $data -and $data[0] -eq 0xFF) "user IMU calibration absent (0x8026)"

    Write-Host "Sub-commands"
    $r = Send-Subcommand 0x48 ([byte[]](0x01))
    Check ($null -ne $r -and ($r[13] -band 0x80)) "enable vibration (0x48) acknowledged"
    $r = Send-Subcommand 0x30 ([byte[]](0x01))
    Check ($null -ne $r -and ($r[13] -band 0x80)) "player lights (0x30) acknowledged"
    $r = Send-Subcommand 0x03 ([byte[]](0x30))
    Check ($null -ne $r -and ($r[13] -band 0x80)) "input mode 0x30 acknowledged"
    $r = Send-Subcommand 0x02
    Check ($null -ne $r -and $r[13] -eq 0x82 -and $r[17] -eq 0x03) "device info (0x02) says Pro"

    Write-Host "Input stream"
    $buf = New-Object byte[] 64
    $count = 0
    $neutral = $true
    $start = Get-Date
    while (((Get-Date) - $start).TotalMilliseconds -lt 1000) {
        $null = $stream.Read($buf, 0, 64)
        if ($buf[0] -ne 0x30) { continue }
        $count++
        $lx = [int]$buf[6] -bor (([int]$buf[7] -band 0x0F) -shl 8)
        if ($buf[3] -ne 0 -or $buf[4] -ne 0 -or $buf[5] -ne 0 -or $lx -ne 2048) { $neutral = $false }
    }
    Write-Host "  $count full reports in 1 s"
    Check ($count -ge 10) "0x30 reports stream continuously"
    Check $neutral "input is neutral (no buttons, sticks centred)"
} finally {
    $stream.Close()
}

if ($script:failures) { Write-Host "$script:failures check(s) failed"; exit 1 }
Write-Host "All device checks passed"
