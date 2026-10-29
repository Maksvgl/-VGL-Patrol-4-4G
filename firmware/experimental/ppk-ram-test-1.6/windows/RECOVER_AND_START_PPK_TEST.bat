@echo off
setlocal
title VGL Patrol 1.6 - RECOVER processor and start PPK RAM test
echo This explicitly erases processor user Flash, UICR and RAM.
echo External W25Q/EEPROM are NOT erased by the recover command.
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -Recover
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -Recover -ProbeSerial "%~1"
)
set "PPK_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %PPK_RESULT%
