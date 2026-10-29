@echo off
setlocal
title VGL Patrol - restore unchanged approved 1.6
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -RestoreApproved
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -RestoreApproved -ProbeSerial "%~1"
)
set "PPK_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %PPK_RESULT%
