# Plays a scripted two-player match on this PC and captures screenshots.
#
# Starts Server.exe and two Client.exe instances whose input comes from
# FORTRESS_AUTOPLAY scripts (see Client/Autoplay.hpp), so nothing is typed
# into the desktop. Only the processes started here are stopped at the end.
#
#   powershell -ExecutionPolicy Bypass -File Tools\autoplay_match.ps1 [-OutDir <dir>]
#
# Output: server.log, client1.log/client2.log (autoplay step logs) and
# shot_<seconds>_p<n>.png window captures in OutDir.

param(
    [string]$Configuration = "Debug",
    [string]$OutDir = "",
    [string]$Player1 = "wait 2; tap 1; wait 2; tap ENTER; wait 3; tap SPACE; wait 6; tap D; wait 14; hold SPACE 1.0; wait 60",
    [string]$Player2 = "wait 3; tap 2; wait 2; tap ENTER; wait 3; tap SPACE; wait 80",
    [int[]]$CaptureAt = @(10, 24, 28, 45),
    [int]$Duration = 50
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot           # Fortress/
$bin = Join-Path $root "x64\$Configuration"
$clientDir = Join-Path $root "Client"              # resources are loaded relative to here
if (-not $OutDir) { $OutDir = Join-Path $root "x64\autoplay" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WinCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@

# The game draws straight to the window DC, so PrintWindow returns a blank
# image; capture the screen area instead. Windows are placed side by side and
# kept on top (without taking focus) so they are not covered.
function Place-Window([System.Diagnostics.Process]$process, [int]$x) {
    for ($i = 0; $i -lt 50 -and $process.MainWindowHandle -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100; $process.Refresh()
    }
    # HWND_TOPMOST, SWP_NOSIZE | SWP_NOACTIVATE
    [void][WinCapture]::SetWindowPos($process.MainWindowHandle, [IntPtr](-1), $x, 0, 0, 0, 0x0011)
}

function Save-Window([System.Diagnostics.Process]$process, [string]$path) {
    $process.Refresh()
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { Write-Output "no window for pid $($process.Id)"; return }
    $rect = New-Object WinCapture+RECT
    [void][WinCapture]::GetWindowRect($handle, [ref]$rect)
    $width = $rect.Right - $rect.Left; $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { return }
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $graphics.Dispose()
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bitmap.Dispose()
}

function Start-Client([string]$script, [string]$log) {
    if (Test-Path $log) { Remove-Item $log }
    $env:FORTRESS_AUTOPLAY = $script
    $env:FORTRESS_AUTOPLAY_LOG = $log
    try {
        return Start-Process -FilePath (Join-Path $bin "Client.exe") -WorkingDirectory $clientDir -PassThru
    } finally {
        Remove-Item Env:FORTRESS_AUTOPLAY, Env:FORTRESS_AUTOPLAY_LOG
    }
}

$started = @()
try {
    $server = Start-Process -FilePath (Join-Path $bin "Server.exe") -ArgumentList "--debug-state" `
        -WorkingDirectory $bin -RedirectStandardOutput (Join-Path $OutDir "server.log") -WindowStyle Hidden -PassThru
    $started += $server
    Start-Sleep -Milliseconds 800

    $clients = @(
        (Start-Client $Player1 (Join-Path $OutDir "client1.log")),
        (Start-Client $Player2 (Join-Path $OutDir "client2.log"))
    )
    $started += $clients
    Place-Window $clients[0] 0
    Place-Window $clients[1] 840

    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    foreach ($at in ($CaptureAt | Sort-Object)) {
        $wait = $at * 1000 - $clock.ElapsedMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
        for ($i = 0; $i -lt $clients.Count; $i++) {
            if (-not $clients[$i].HasExited) {
                Save-Window $clients[$i] (Join-Path $OutDir ("shot_{0:D3}_p{1}.png" -f $at, ($i + 1)))
            }
        }
    }

    $remaining = $Duration * 1000 - $clock.ElapsedMilliseconds
    if ($remaining -gt 0) { Start-Sleep -Milliseconds $remaining }

    foreach ($process in $started) {
        if ($process.HasExited) { Write-Output "$($process.ProcessName) $($process.Id) exited early with code $($process.ExitCode)" }
    }
}
finally {
    foreach ($process in $started) {
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}

Write-Output "output in $OutDir"
