@echo off
setlocal

cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\gx12-launcher-v3.ps1"
set "RESULT=%ERRORLEVEL%"

if not "%RESULT%"=="0" (
  echo.
  echo GX12 launcher V3 exited with code %RESULT%.
  pause
)

exit /b %RESULT%
