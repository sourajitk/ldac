@echo off
setlocal enabledelayedexpansion

echo =================================================================
echo   BUILDING SONY LDAC HIGH-RESOLUTION BLUETOOTH DRIVER SUITE
echo =================================================================
echo.

if not exist "bin" mkdir bin

echo [1/5] Compiling Unified LDAC Codec DLL (Encoder + Decoder + ABR)...
clang.exe -shared -O3 -DLDAC_BUILD_DLL -D_CRT_SECURE_NO_WARNINGS ^
    -Iinclude -Iinclude_win32 -IldacBT_src/libldac/inc -IldacBT_src/libldac/src -IldacBT_src/libldac/abr/inc -Ilibldacdec_src ^
    -o bin\ldac_codec.dll src\codec\ldacBT_enc_unified.c src\codec\ldacdec_unified.c src\codec\ldac_codec_unified.c ^
    -Wl,/DEF:src\codec\ldac_codec.def -lws2_32
if %errorlevel% neq 0 ( echo [ERROR] Failed to compile ldac_codec.dll & exit /b 1 )
echo   - bin\ldac_codec.dll and bin\ldac_codec.lib created.

echo.
echo [2/5] Compiling Unit Test Suite...
clang++.exe -O2 -Iinclude -Iinclude_win32 tests\test_ldac_codec.cpp bin\ldac_codec.lib -o bin\test_ldac_codec.exe
if %errorlevel% neq 0 ( echo [ERROR] Failed to compile test_ldac_codec.exe & exit /b 1 )
echo   - bin\test_ldac_codec.exe created.

echo.
echo [3/5] Compiling LDAC Command Line Interface (CLI)...
clang++.exe -O2 -Iinclude -Iinclude_win32 src\cli\ldac_cli.cpp src\bluetooth\bth_manager.cpp src\audio\wasapi_streamer.cpp ^
    bin\ldac_codec.lib -lole32 -lksuser -lws2_32 -lsetupapi -lbthprops -ladvapi32 -o bin\ldac_cli.exe
if %errorlevel% neq 0 ( echo [ERROR] Failed to compile ldac_cli.exe & exit /b 1 )
echo   - bin\ldac_cli.exe created.

echo.
echo [4/5] Compiling Background Driver Service Daemon...
clang++.exe -O2 -Iinclude -Iinclude_win32 src\service\ldac_service.cpp src\bluetooth\bth_manager.cpp src\audio\wasapi_streamer.cpp ^
    bin\ldac_codec.lib -lole32 -lksuser -lws2_32 -lsetupapi -lbthprops -ladvapi32 -o bin\ldac_service.exe
if %errorlevel% neq 0 ( echo [ERROR] Failed to compile ldac_service.exe & exit /b 1 )
echo   - bin\ldac_service.exe created.

echo.
echo [5/5] Compiling Modern Dark-Mode GUI Control Panel...
clang++.exe -O2 -Wl,/subsystem:windows -Iinclude -Iinclude_win32 src\gui\control_panel.cpp src\bluetooth\bth_manager.cpp src\audio\wasapi_streamer.cpp ^
    bin\ldac_codec.lib -lole32 -lksuser -lws2_32 -lsetupapi -lbthprops -lcomctl32 -lshell32 -lgdi32 -luser32 -ladvapi32 -o bin\ldac_control_panel.exe
if %errorlevel% neq 0 ( echo [ERROR] Failed to compile ldac_control_panel.exe & exit /b 1 )
echo   - bin\ldac_control_panel.exe created.

echo.
echo =================================================================
echo   ALL TARGETS BUILT SUCCESSFULLY!
echo =================================================================
echo.
echo Binaries located in .\bin\:
echo   - ldac_codec.dll         (High-performance LDAC Encoder + Decoder native library)
echo   - test_ldac_codec.exe    (Comprehensive unit tests)
echo   - ldac_cli.exe           (Device scanner, file encoder/decoder, benchmark, streaming)
echo   - ldac_service.exe       (Background auto-connection Windows service)
echo   - ldac_control_panel.exe (Modern dark-mode GUI control panel)
echo.
