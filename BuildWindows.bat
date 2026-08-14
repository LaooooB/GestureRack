@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1"
set CODE=%ERRORLEVEL%
echo.
if not "%CODE%"=="0" (
  echo Build failed with exit code %CODE%.
) else (
  echo Build completed successfully.
)
pause
exit /b %CODE%
