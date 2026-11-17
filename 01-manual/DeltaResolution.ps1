param([switch]$Restore, [switch]$Check)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ScreenMode {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumDisplaySettings(string device, int mode, ref DEVMODE settings);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int ChangeDisplaySettings(ref DEVMODE settings, int flags);
    public static DEVMODE NewMode() { var m = new DEVMODE(); m.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE)); return m; }
    public static DEVMODE Current() { var m = NewMode(); if (!EnumDisplaySettings(null, -1, ref m)) throw new Exception("Cannot read the current display mode."); return m; }
    public static DEVMODE Find(int width, int height, int frequency, int bits) {
        DEVMODE best = NewMode(); int score = Int32.MaxValue; bool found = false;
        for (int i = 0; ; i++) { var m = NewMode(); if (!EnumDisplaySettings(null, i, ref m)) break;
            if (m.dmPelsWidth != width || m.dmPelsHeight != height || m.dmBitsPerPel != bits) continue;
            if (ChangeDisplaySettings(ref m, 2) != 0) continue;
            int s = Math.Abs(m.dmDisplayFrequency - frequency);
            if (s < score) { score = s; best = m; found = true; }
        }
        if (!found) {
            var candidate = Current();
            candidate.dmPelsWidth = width; candidate.dmPelsHeight = height;
            if (ChangeDisplaySettings(ref candidate, 2) == 0) return candidate;
            throw new Exception("Cannot validate display mode " + width + "x" + height + " in this session.");
        }
        return best;
    }
    public static void Apply(DEVMODE m) { int result = ChangeDisplaySettings(ref m, 0); if (result != 0) throw new Exception("ChangeDisplaySettings failed: " + result); }
}
'@
try {
    $current = [ScreenMode]::Current()
    Write-Host ("Current: {0}x{1} @ {2} Hz" -f $current.dmPelsWidth, $current.dmPelsHeight, $current.dmDisplayFrequency)
    if ($Check) {
        foreach ($size in @(@(2880,2160), @(3840,2160))) {
            $m = [ScreenMode]::Find($size[0], $size[1], $current.dmDisplayFrequency, $current.dmBitsPerPel)
            Write-Host ("Supported: {0}x{1} @ {2} Hz" -f $m.dmPelsWidth, $m.dmPelsHeight, $m.dmDisplayFrequency)
        }
        return
    }
    $back = [ScreenMode]::Find(3840, 2160, $current.dmDisplayFrequency, $current.dmBitsPerPel)
    if ($Restore) { [ScreenMode]::Apply($back); Write-Host 'Restored to 3840x2160.'; return }
    if (Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue) { throw 'The game is already running. Exit the game first.' }
    $target = [ScreenMode]::Find(2880, 2160, $current.dmDisplayFrequency, $current.dmBitsPerPel)
    try {
        [ScreenMode]::Apply($target)
        Write-Host 'Switched to 2880x2160. Start Delta Force normally now.'
        Write-Host 'Waiting for the game. Press Q to cancel and restore.'
        while (-not (Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)) {
            if ([Console]::KeyAvailable -and [Console]::ReadKey($true).Key -eq 'Q') { return }
            Start-Sleep -Seconds 2
        }
        Write-Host 'Game detected. Resolution will be restored after it exits.'
        do { Start-Sleep -Seconds 3 } while (Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)
    } finally {
        [ScreenMode]::Apply($back)
        Write-Host 'Restored to 3840x2160.'
    }
} catch {
    Write-Host ("Error: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
