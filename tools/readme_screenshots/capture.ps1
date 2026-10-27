# Retakes the README screenshots (docs/images/screen-*.png) with the desktop simulator and
# fake_moonraker. Build the win32-mingw64 Debug preset first.
#
#   powershell -ExecutionPolicy Bypass -File tools/readme_screenshots/capture.ps1
#
# The simulator window does not need to be visible: keys are posted to it, each shot is the
# app's own screenshot (PrintScreen -> LVGL snapshot, 320x170) composited onto the device shell
# at (155, 21) the way DesktopSimulatorFrame renders it. Port 7125 must be free.
param(
    [string]$BinDir = '',
    [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class KdzSimWindow {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
    public static IntPtr Find(uint pid, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            var sb = new StringBuilder(256); GetWindowText(h, sb, 256);
            if (p == pid && sb.ToString() == title) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$BinDir) { $BinDir = Join-Path $Repo 'build\mingw64\Debug' }
if (!$OutDir) { $OutDir = Join-Path $Repo 'docs\images' }
$SimHome = Join-Path $env:TEMP 'kdz-readme-screenshots'
$ShotDir = Join-Path $SimHome 'Pictures\Screenshots'
$ConfigFile = Join-Path $Repo 'config\klipper-deck-zero.conf'

$WM_KEYDOWN = 0x0100
$WM_KEYUP = 0x0101
$WM_CHAR = 0x0102
# Keys sent as key presses; anything else is sent as typed text. The simulator takes 4 as a key
# press (SDLK_4), the other digits and letters as text.
$VirtualKeys = @{ 'ESC' = 0x1B; 'ENTER' = 0x0D; 'UP' = 0x26; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27; '4' = 0x34 }
$ExtendedKeys = @('UP', 'DOWN', 'LEFT', 'RIGHT')

function Start-Sim([string]$Print = '') {
    New-Item -ItemType Directory -Force $ShotDir | Out-Null
    Remove-Item (Join-Path $ShotDir '*.png') -ErrorAction SilentlyContinue
    $fakeArgs = @('--port', '7125', '--speed', '20')
    if ($Print) { $fakeArgs += @('--print', "`"$Print`"") }
    $script:Fake = Start-Process -FilePath (Join-Path $BinDir 'fake_moonraker.exe') -ArgumentList $fakeArgs -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput (Join-Path $SimHome 'fake.log') -RedirectStandardError (Join-Path $SimHome 'fake.err')
    Start-Sleep -Milliseconds 800

    # Screenshots go to $HOME/Pictures/Screenshots.
    $env:HOME = $SimHome
    $script:App = Start-Process -FilePath (Join-Path $BinDir 'klipper-deck-zero.exe') -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput (Join-Path $SimHome 'app.log') -RedirectStandardError (Join-Path $SimHome 'app.err')
    # SDL recreates the window when it sets up the OpenGL renderer, so look it up once it settled.
    Start-Sleep -Seconds 4
    $script:Hwnd = [KdzSimWindow]::Find([uint32]$script:App.Id, 'KlipperDeckZero')
    if ($script:Hwnd -eq [IntPtr]::Zero) { throw 'simulator window not found' }
}

function Stop-Sim {
    if ($script:App -and !$script:App.HasExited) { $script:App.Kill() }
    if ($script:Fake -and !$script:Fake.HasExited) { $script:Fake.Kill() }
}

function Send-VirtualKey([int]$Vk, [int]$Scan, [bool]$Extended) {
    $lParam = 1 -bor ($Scan -shl 16)
    if ($Extended) { $lParam = $lParam -bor (1 -shl 24) }
    [KdzSimWindow]::PostMessage($script:Hwnd, $WM_KEYDOWN, [IntPtr]$Vk, [IntPtr]$lParam) | Out-Null
    Start-Sleep -Milliseconds 60
    $up = [int64]$lParam -bor (3L -shl 30)
    [KdzSimWindow]::PostMessage($script:Hwnd, $WM_KEYUP, [IntPtr]$Vk, [IntPtr]$up) | Out-Null
}

function Send-Keys([string[]]$Keys) {
    foreach ($key in $Keys) {
        if ($VirtualKeys.ContainsKey($key)) {
            $vk = $VirtualKeys[$key]
            Send-VirtualKey $vk ([KdzSimWindow]::MapVirtualKey([uint32]$vk, 0)) ($ExtendedKeys -contains $key)
        }
        else {
            [KdzSimWindow]::PostMessage($script:Hwnd, $WM_CHAR, [IntPtr][int][char]$key, [IntPtr]1) | Out-Null
        }
        Start-Sleep -Milliseconds 500
    }
}

function New-Shell([bool]$Dark) {
    $name = if ($Dark) { 'cp0-dark.png' } else { 'cp0-light.png' }
    $source = [System.Drawing.Image]::FromFile((Join-Path $Repo "assets\images\$name"))
    $shell = New-Object System.Drawing.Bitmap 630, 405
    $graphics = [System.Drawing.Graphics]::FromImage($shell)
    $graphics.Clear([System.Drawing.Color]::FromArgb(0x20, 0x21, 0x24))
    $graphics.InterpolationMode = 'HighQualityBicubic'
    $graphics.PixelOffsetMode = 'HighQuality'
    $graphics.DrawImage($source, 0, 0, 630, 405)
    $graphics.Dispose(); $source.Dispose()
    return $shell
}

function Save-Shot([string]$Name, [switch]$Dark) {
    $before = @(Get-ChildItem $ShotDir -Filter '*.png' | ForEach-Object Name)
    Send-VirtualKey 0x2C 0x37 $true  # PrintScreen
    $new = $null
    for ($i = 0; $i -lt 30 -and !$new; $i++) {
        Start-Sleep -Milliseconds 200
        $new = Get-ChildItem $ShotDir -Filter '*.png' | Where-Object { $before -notcontains $_.Name } | Select-Object -First 1
    }
    if (!$new) { throw "the app saved no screenshot for $Name" }
    Start-Sleep -Milliseconds 300

    $screen = [System.Drawing.Image]::FromFile($new.FullName)
    $shell = New-Shell $Dark.IsPresent
    $graphics = [System.Drawing.Graphics]::FromImage($shell)
    $graphics.DrawImage($screen, 155, 21, $screen.Width, $screen.Height)
    $graphics.Dispose()
    $path = Join-Path $OutDir $Name
    $shell.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $shell.Dispose(); $screen.Dispose()
    Write-Host "saved $path"
    # Let the "Screenshot saved" toast go away before the next shot.
    Start-Sleep -Seconds 3
}

# Toggling the theme with D is saved to the desktop config; put it back afterwards.
$savedConfig = [System.IO.File]::ReadAllBytes($ConfigFile)
try {
    try {
        Start-Sim
        Save-Shot 'screen-status-ready.png'
        Send-Keys @('5'); Save-Shot 'screen-temperature.png'
        Send-Keys @('6'); Save-Shot 'screen-extruder.png'
        Send-Keys @('7'); Start-Sleep -Seconds 1; Save-Shot 'screen-files.png'
        Send-Keys @('ENTER'); Start-Sleep -Seconds 1; Save-Shot 'screen-files-detail.png'
        # Switching to layer 2 keeps the page; its slot 1 (key 4) opens Move.
        Send-Keys @('ESC', '>', '4'); Save-Shot 'screen-move.png'
        Send-Keys @('5'); Save-Shot 'screen-settings.png'
    }
    finally { Stop-Sim }

    Start-Sleep -Seconds 1
    try {
        Start-Sim -Print '3DBenchy_PLA_0.2mm.gcode'
        # Wait for the heaters and a few layers.
        Start-Sleep -Seconds 14
        Save-Shot 'screen-status-printing.png'
        Send-Keys @('8'); Save-Shot 'screen-control.png'
        Send-Keys @('4', 'd'); Start-Sleep -Seconds 1; Save-Shot 'screen-status-printing-dark.png' -Dark
    }
    finally { Stop-Sim }
}
finally {
    [System.IO.File]::WriteAllBytes($ConfigFile, $savedConfig)
}
