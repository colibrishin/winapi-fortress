@echo off
rem Double-click: local two-player game (see play_local.ps1 for options).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_local.ps1" %*
