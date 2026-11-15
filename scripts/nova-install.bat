@echo off
setlocal EnableExtensions
title NOVA - command line installer
rem ==================================================================
rem  NOVA - open source firmware for the ATS Mini (ESP32-S3 + SI4732)
rem  Command line installer for Windows. The easiest way is
rem  NOVA-Installer.html; use this script if your browser cannot.
rem  Uses Espressif's official esptool, downloaded on first use.
rem ==================================================================

set "ESPTOOL_VER=5.6.0"
set "HERE=%~dp0"
set "ROOT=%HERE%.."
set "TOOLS=%HERE%tools"
set "ESPTOOL=%TOOLS%\esptool-windows-amd64\esptool.exe"
set "BACKUPS=%ROOT%\backups"

call :get_esptool || goto :fail
call :ask_port

:menu
echo.
echo  ============================================
echo    NOVA command line installer
echo  ============================================
echo    1  Back up the radio's current firmware (8 MB, about 2 min)
echo    2  Clean install of NOVA (erases everything)
echo    3  Update NOVA (keeps stations and settings)
echo    4  Restore a backup
echo    5  Quit
echo.
choice /c 12345 /n /m "  Choose 1-5: "
if errorlevel 5 exit /b 0
if errorlevel 4 goto :restore
if errorlevel 3 goto :update
if errorlevel 2 goto :clean
goto :backup

:backup
if not exist "%BACKUPS%" mkdir "%BACKUPS%"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmm"') do set "STAMP=%%i"
set "OUT=%BACKUPS%\ats-mini-backup-%STAMP%.bin"
echo.
echo  Reading the whole flash to "%OUT%"...
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 read-flash 0 ALL "%OUT%" || goto :fail
echo  Backup saved.
goto :menu

:clean
call :variant || goto :fail
echo.
echo  Clean install of NOVA (%VARIANT%). Everything on the radio is erased.
choice /c YN /m "  Continue"
if errorlevel 2 goto :menu
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 "%ROOT%\firmware\%VARIANT%\nova.ino.merged.bin" || goto :fail
call :done
goto :menu

:update
call :variant || goto :fail
echo.
echo  Updating NOVA (%VARIANT%): stations, settings and station names are kept.
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x10000 "%ROOT%\firmware\%VARIANT%\nova.ino.bin" || goto :fail
call :done
goto :menu

:restore
echo.
echo  Backups found:
dir /b "%BACKUPS%\*.bin" 2>nul || echo   (none in "%BACKUPS%")
set /p "RFILE=  File name or full path: "
if exist "%BACKUPS%\%RFILE%" set "RFILE=%BACKUPS%\%RFILE%"
if not exist "%RFILE%" (echo  File not found. & goto :menu)
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 write-flash -z 0x0 "%RFILE%" || goto :fail
echo  Backup restored. The radio restarts.
goto :menu

rem ---------------- helpers ----------------
:variant
set "INFO=%TEMP%\nova-chip.txt"
"%ESPTOOL%" --chip esp32s3 %PORTARG% flash-id > "%INFO%" 2>&1
type "%INFO%" | findstr /C:"Chip" /C:"PSRAM" /C:"flash size"
findstr /C:"Detected flash size" "%INFO%" >nul || (echo  The radio did not answer. Hold BOOT while plugging it in, then retry. & exit /b 1)
set "VARIANT="
findstr /C:"PSRAM 8MB" "%INFO%" >nul && set "VARIANT=ospi"
findstr /C:"PSRAM 16MB" "%INFO%" >nul && set "VARIANT=ospi"
findstr /C:"PSRAM 2MB" "%INFO%" >nul && set "VARIANT=qspi"
findstr /C:"PSRAM 4MB" "%INFO%" >nul && set "VARIANT=qspi"
if defined VARIANT exit /b 0
echo  The memory type was not reported. Most ATS Mini radios use OSPI.
choice /c OQ /m "  Build: O = OSPI (most radios), Q = QSPI"
if errorlevel 2 (set "VARIANT=qspi") else (set "VARIANT=ospi")
exit /b 0

:done
echo.
echo  NOVA is installed. The radio restarts and shows the NOVA star.
echo  Open NOVA-Editor.html in Chrome or Edge for station names, the clock and all settings.
exit /b 0

:get_esptool
if exist "%ESPTOOL%" exit /b 0
echo  Downloading esptool %ESPTOOL_VER% (Espressif, official)...
if not exist "%TOOLS%" mkdir "%TOOLS%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; $ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/espressif/esptool/releases/download/v%ESPTOOL_VER%/esptool-v%ESPTOOL_VER%-windows-amd64.zip' -OutFile '%TOOLS%\esptool.zip'; Expand-Archive -Force '%TOOLS%\esptool.zip' '%TOOLS%'"
if exist "%ESPTOOL%" exit /b 0
echo  esptool could not be downloaded. Check the internet connection.
exit /b 1

:ask_port
echo.
echo  Serial ports on this computer:
powershell -NoProfile -Command "Get-CimInstance Win32_SerialPort | ForEach-Object { '   ' + $_.DeviceID + '  ' + $_.Name }; Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | ForEach-Object { '   ' + $_.FriendlyName }"
set "PORT="
set /p "PORT=  Radio port (for example COM5), or Enter to find it automatically: "
set "PORTARG="
if defined PORT set "PORTARG=--port %PORT%"
exit /b 0

:fail
echo.
echo  *** Something went wrong. Nothing else was changed. ***
echo  Check the cable, close other programs that use the radio, and try again.
pause
exit /b 1
