@echo off
setlocal
title VGL Patrol 1.6 - start PPK RAM test without reflashing
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -ProbeSerial "%~1"
)
set "PPK_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %PPK_RESULT%
