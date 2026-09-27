@echo off
setlocal enabledelayedexpansion

echo =================================================================
echo   SONY LDAC BLUETOOTH DRIVER UNINSTALLATION
echo =================================================================
echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] This uninstaller requires Administrator privileges.
    echo Please right-click this script and select "Run as administrator".
    pause
    exit /b 1
)

echo [1/3] Stopping and removing LdacAudioService...
sc stop LdacAudioService >nul 2>&1
sc delete LdacAudioService >nul 2>&1

echo [2/3] Removing Driver Package...
for /f "tokens=1" %%d in ('pnputil /enum-drivers ^| findstr /i "ldac_a2dp_driver.inf"') do (
    pnputil /delete-driver %%d /uninstall /force >nul 2>&1
)

echo [3/3] Removing Files and Registry keys...
reg delete "HKLM\SOFTWARE\LDAC-Windows" /f >nul 2>&1
if exist "%SystemRoot%\LDAC" rd /s /q "%SystemRoot%\LDAC" >nul 2>&1
if exist "%SystemRoot%\System32\ldac_codec.dll" del /f /q "%SystemRoot%\System32\ldac_codec.dll" >nul 2>&1

echo.
echo =================================================================
echo   UNINSTALLATION COMPLETE!
echo =================================================================
echo.
pause
