# Starts a local two-player game on this PC: Server.exe and two Client.exe
# windows side by side. By default both clients go through the menus on
# their own (player 1/2, lobby, room, character, all four items) and player 1
# starts the map; then the keyboard is handed to you. Click a window to
# control that player. Press H in the game for the key list.
#
#   Tools\play_local.bat                        (double-click: desert, cannon vs missile)
#   powershell -ExecutionPolicy Bypass -File Tools\play_local.ps1 -Map SkyValley -Player1 secwind -Player2 cannon
#   ... -Manual     menus by hand too
#   ... -Build      build the solution (Debug x64) first
#
# Closing both client windows stops the server. Only processes started here
# are stopped.

param(
    [ValidateSet("Desert", "SkyValley")][string]$Map = "Desert",
    [ValidateSet("cannon", "missile", "secwind")][string]$Player1 = "cannon",
    [ValidateSet("cannon", "missile", "secwind")][string]$Player2 = "missile",
    [switch]$Manual,
    [switch]$Build,
    [string]$Configuration = "Debug",
    [int]$CloseAfter = 0    # for tests: stop everything after this many seconds
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot           # Fortress/
$bin = Join-Path $root "x64\$Configuration"
$clientDir = Join-Path $root "Client"              # resources are loaded relative to here

if ($Build) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) { throw "MSBuild not found" }
    & $msbuild (Join-Path $root "Fortress.sln") /m /p:Configuration=$Configuration /p:Platform=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

foreach ($exe in "Server.exe", "Client.exe") {
    if (-not (Test-Path (Join-Path $bin $exe))) { throw "$exe not found in $bin (run with -Build)" }
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PlayWindow {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@

function Place-Window([System.Diagnostics.Process]$process, [int]$x) {
    for ($i = 0; $i -lt 50 -and $process.MainWindowHandle -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100; $process.Refresh()
    }
    # SWP_NOSIZE | SWP_NOZORDER
    [void][PlayWindow]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, $x, 0, 0, 0, 0x0005)
}

function Start-Client([string]$script) {
    if ($script) { $env:FORTRESS_AUTOPLAY = $script }
    try {
        return Start-Process -FilePath (Join-Path $bin "Client.exe") -WorkingDirectory $clientDir -PassThru
    } finally {
        Remove-Item Env:FORTRESS_AUTOPLAY -ErrorAction SilentlyContinue
    }
}

# Room keys (Client/RoomScene.cpp): 1-3 character, Q/W/E/R items, D/S map.
$characterKey = @{ cannon = "1"; missile = "2"; secwind = "3" }
$mapKey = @{ Desert = "D"; SkyValley = "S" }
$menu = "tap ENTER; wait 1; tap SPACE; wait 1.5"   # bulletin board -> lobby -> room
$items = "tap Q; tap W; tap E; tap R"

$script1 = ""; $script2 = ""
if (-not $Manual) {
    # Player 2 is in the room before player 1 starts the map.
    $script1 = "wait 1.5; tap 1; wait 1; $menu; tap $($characterKey[$Player1]); $items; wait 3; tap $($mapKey[$Map]); manual"
    $script2 = "wait 2; tap 2; wait 1; $menu; tap $($characterKey[$Player2]); $items; manual"
}

$started = @()
try {
    $server = Start-Process -FilePath (Join-Path $bin "Server.exe") -WorkingDirectory $bin -WindowStyle Minimized -PassThru
    $started += $server
    Start-Sleep -Milliseconds 500

    $client1 = Start-Client $script1; $started += $client1
    Place-Window $client1 0
    $client2 = Start-Client $script2; $started += $client2
    Place-Window $client2 820

    if ($Manual) {
        Write-Output "Left window: press 1 (player 1). Right window: press 2 (player 2). Then ENTER, SPACE, pick a character (1-3), and one player presses D (desert) or S (sky valley)."
    } else {
        Write-Output "Setting up the match ($Map, $Player1 vs $Player2)... the keyboard is yours once the map loads."
    }
    Write-Output "Click a window to control that player. Press H in the game for the key list. Close both windows to stop."

    $deadline = if ($CloseAfter -gt 0) { (Get-Date).AddSeconds($CloseAfter) } else { [DateTime]::MaxValue }
    while (-not ($client1.HasExited -and $client2.HasExited) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
} finally {
    foreach ($process in $started) {
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
