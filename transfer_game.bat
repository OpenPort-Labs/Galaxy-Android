@echo off
setlocal
cd /d "%~dp0"
set ADB="C:\Users\carlo\AppData\Local\Android\Sdk\platform-tools\adb.exe"

echo ==============================================
echo GalaxyMobile - 1-Click Game Data Transfer
echo ==============================================
echo.

echo [1/3] Checking connected Android device...
%ADB% devices
echo.

if not exist "cooked\sys\fst.bin" (
    echo [ERROR] No cooked game files found in 'cooked\'.
    echo Please make sure you have cooked your game files first:
    echo   py tools\cook\cook.py extracted cooked --with-movies
    echo.
    pause
    exit /b 1
)

echo [2/3] Transferring game data to Android device...
echo (Packing and streaming ~3.3 GB of assets over USB, please wait a few minutes...)
echo.
py tools\push_data.py cooked

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Transfer failed!
    echo Please make sure:
    echo  1. Phone is connected via USB.
    echo  2. Phone screen is unlocked.
    echo  3. USB debugging is allowed.
    echo.
    pause
    exit /b 1
)

echo.
echo ==============================================
echo [3/3] Game assets successfully transferred!
echo You can now launch GalaxyMobile on your phone.
echo ==============================================
echo.
pause
