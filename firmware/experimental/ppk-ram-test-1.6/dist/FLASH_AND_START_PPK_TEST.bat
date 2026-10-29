@echo off
setlocal
title VGL Patrol 1.6 - flash and start PPK RAM test
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -ProbeSerial "%~1"
)
set "PPK_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %PPK_RESULT%
