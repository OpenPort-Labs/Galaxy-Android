@echo off
setlocal
cd /d "%~dp0"

echo ==============================================
echo Pushing GalaxyMobile to GitHub
echo ==============================================
echo.
echo Target: https://github.com/sebsies/GalaxyMobile
echo.

git push -u origin master

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Push failed or was canceled.
    echo.
    pause
    exit /b 1
)

echo.
echo ==============================================
echo [SUCCESS] Successfully pushed to GitHub!
echo View your repository: https://github.com/sebsies/GalaxyMobile
echo ==============================================
echo.
pause
