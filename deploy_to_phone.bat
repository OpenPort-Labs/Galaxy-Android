@echo off
setlocal
cd /d "%~dp0"
set ADB="C:\Users\carlo\AppData\Local\Android\Sdk\platform-tools\adb.exe"

echo ==============================================
echo GalaxyMobile Deployment Helper
echo ==============================================
echo.
echo Checking for connected Android device...
%ADB% devices

echo.
echo Installing updated GalaxyMobile.apk...
%ADB% install --no-incremental -r "out-mobile\GalaxyMobile.apk"
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Installation failed!
    echo Please make sure:
    echo  1. Phone is plugged into USB.
    echo  2. Phone screen is unlocked.
    echo  3. Tap 'Allow' on the 'Allow USB debugging?' dialog on your phone.
    echo.
    pause
    exit /b 1
)

echo.
echo Launching GalaxyMobile on device...
%ADB% shell am start -n com.galaxy.quest/android.app.NativeActivity
echo.
echo [SUCCESS] GalaxyMobile updated and launched!
echo.
pause
