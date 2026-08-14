@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" -Clean
set CODE=%ERRORLEVEL%
echo.
if not "%CODE%"=="0" (
  echo Clean build failed with exit code %CODE%.
) else (
  echo Clean build completed successfully.
)
pause
exit /b %CODE%
