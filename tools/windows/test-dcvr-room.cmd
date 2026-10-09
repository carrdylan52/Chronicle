@echo off
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0test-dcvr.ps1" -Mode Room -Seconds 60
pause
