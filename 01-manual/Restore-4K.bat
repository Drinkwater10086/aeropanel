@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DeltaResolution.ps1" -Restore
echo.
pause
