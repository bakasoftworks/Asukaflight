@echo off
setlocal

cd /d "%~dp0"

set "WPF_PROJECT=%CD%\apps\Gx12.Launcher.Wpf\Gx12.Launcher.Wpf.csproj"
set "WPF_EXE=%CD%\apps\Gx12.Launcher.Wpf\bin\Debug\net7.0-windows\Asukaflight.exe"

dotnet build "%WPF_PROJECT%"
if errorlevel 1 (
  echo.
  echo Asukaflight WPF build failed.
  pause
  exit /b 1
)

"%WPF_EXE%"
set "RESULT=%ERRORLEVEL%"

if not "%RESULT%"=="0" (
  echo.
  echo Asukaflight exited with code %RESULT%.
  pause
)

exit /b %RESULT%
