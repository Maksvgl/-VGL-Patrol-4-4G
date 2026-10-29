@echo off
chcp 65001 >nul
title VGL Patrol 1.6 - S132 - update bootloader

echo ========================================================
echo VGL Patrol 1.6 - 1400 ms - RF 5 ms
echo SoftDevice S132 + application + BIN update bootloader
echo ========================================================
echo.

where nrfjprog >nul 2>nul
if errorlevel 1 goto NO_TOOL

nrfjprog -f NRF52 --reset
if errorlevel 1 goto ERROR

nrfjprog -f NRF52 --program "%~dp0VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex" --sectorerase --verify
if errorlevel 1 goto ERROR

nrfjprog -f NRF52 --reset
if errorlevel 1 goto ERROR

echo.
echo SUCCESS. Firmware 1.6, S132 and update bootloader programmed.
pause
exit /b 0

:NO_TOOL
echo ERROR: nrfjprog not found.
pause
exit /b 1

:ERROR
echo ERROR: flashing failed.
pause
exit /b 1
