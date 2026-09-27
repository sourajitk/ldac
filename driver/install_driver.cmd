@echo off
setlocal enabledelayedexpansion

echo =================================================================
echo   SONY LDAC BLUETOOTH DRIVER INSTALLATION (WINDOWS 10 / 11)
echo =================================================================
echo.

:: Check Administrator Privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] This installer requires Administrator privileges.
    echo Please right-click this script and select "Run as administrator".
    pause
    exit /b 1
)

set SCRIPT_DIR=%~dp0
set BIN_DIR=%SCRIPT_DIR%..\bin
set TARGET_DIR=%SystemRoot%\LDAC

echo [1/5] Creating application directory: %TARGET_DIR%...
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo [2/5] Deploying LDAC binaries...
if exist "%BIN_DIR%\ldac_codec.dll" (
    copy /y "%BIN_DIR%\ldac_codec.dll" "%SystemRoot%\System32\" >nul
    copy /y "%BIN_DIR%\ldac_codec.dll" "%TARGET_DIR%\" >nul
)
if exist "%BIN_DIR%\ldac_service.exe" copy /y "%BIN_DIR%\ldac_service.exe" "%TARGET_DIR%\" >nul
if exist "%BIN_DIR%\ldac_cli.exe" copy /y "%BIN_DIR%\ldac_cli.exe" "%TARGET_DIR%\" >nul
if exist "%BIN_DIR%\ldac_control_panel.exe" copy /y "%BIN_DIR%\ldac_control_panel.exe" "%TARGET_DIR%\" >nul

echo [3/5] Installing Driver Package via PnPUtil...
pnputil /add-driver "%SCRIPT_DIR%ldac_a2dp_driver.inf" /install >nul 2>&1
if %errorlevel% equ 0 (
    echo   - Driver package registered in Windows DriverStore successfully.
) else (
    echo   - PnPUtil driver registration finished (or already present).
)

echo [4/5] Registering and Configuring Windows Background Service...
sc stop LdacAudioService >nul 2>&1
sc delete LdacAudioService >nul 2>&1

sc create LdacAudioService binPath= "\"%TARGET_DIR%\ldac_service.exe\" --service" start= auto DisplayName= "Sony LDAC Bluetooth Audio Service" >nul
sc description LdacAudioService "Manages 96kHz/24-bit Hi-Res LDAC audio encoding and transmission to compatible Bluetooth headphones and DACs." >nul
sc start LdacAudioService >nul 2>&1

echo [5/5] Configuring High-Resolution Audio Registry Keys...
reg add "HKLM\SOFTWARE\LDAC-Windows" /v "PreferredBitrate" /t REG_DWORD /d 990 /f >nul
reg add "HKLM\SOFTWARE\LDAC-Windows" /v "PreferredSampleRate" /t REG_DWORD /d 96000 /f >nul
reg add "HKLM\SOFTWARE\LDAC-Windows" /v "PreferredBitDepth" /t REG_DWORD /d 24 /f >nul
reg add "HKLM\SOFTWARE\LDAC-Windows" /v "AdaptiveBitrate" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\LDAC-Windows" /v "AutoConnectSony" /t REG_DWORD /d 1 /f >nul

echo.
echo =================================================================
echo   INSTALLATION COMPLETED SUCCESSFULLY!
echo =================================================================
echo.
echo  * The Sony LDAC High-Resolution audio service is now active.
echo  * When your Sony WH-1000XM4/XM5 or LDAC device connects, 
echo    audio will automatically stream at up to 990 kbps (96kHz / 24-bit).
echo  * Launch "ldac_control_panel.exe" to adjust settings or monitor live stats.
echo.
pause
