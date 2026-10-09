@echo off
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0test-dcvr.ps1" -Mode Norune -Seconds 120
pause
