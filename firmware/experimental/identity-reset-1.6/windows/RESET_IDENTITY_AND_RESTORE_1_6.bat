@echo off
setlocal
title VGL Patrol - reset identity and restore ordinary 1.6
echo This explicitly erases the processor and permanently clears its external activation, secret and number.
echo Keep board power connected until FACTORY_RESET_COMPLETE.
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Reset_Identity.ps1"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Reset_Identity.ps1" -ProbeSerial "%~1"
)
set "RESET_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESET_RESULT%
