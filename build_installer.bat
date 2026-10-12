@echo off
setlocal
cd /d "%~dp0"

echo ==============================================
echo Building GalaxyInstaller APK
echo ==============================================
echo.

where py >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "PYTHON_EXE=py"
) else (
    where python >nul 2>nul
    if %ERRORLEVEL% equ 0 (
        set "PYTHON_EXE=python"
    ) else (
        set "PYTHON_EXE=C:\Users\carlo\.local\bin\python3.12.exe"
    )
)

"%PYTHON_EXE%" installer-mobile\build.py

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed!
    echo.
    if "%1" neq "--no-pause" pause
    exit /b 1
)

echo.
echo ==============================================
echo [SUCCESS] out-mobile\GalaxyInstaller.apk created!
echo ==============================================
if "%1" neq "--no-pause" pause
