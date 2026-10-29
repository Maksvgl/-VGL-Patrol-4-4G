@echo off
setlocal
title VGL Patrol - RECOVER processor and restore approved 1.6
echo This explicitly erases processor user Flash, UICR and RAM.
echo External W25Q/EEPROM are NOT erased by the recover command.
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -Recover -RestoreApproved
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PPK_Test.ps1" -Flash -Recover -RestoreApproved -ProbeSerial "%~1"
)
set "PPK_RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %PPK_RESULT%
