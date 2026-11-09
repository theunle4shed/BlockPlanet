@echo off
rem Returns to the normal build (the main branch) after trying another branch, and rebuilds.
if not "%~1"=="--from-temp" (
  copy /y "%~f0" "%TEMP%\blockplanet-back-main.bat" >nul
  "%TEMP%\blockplanet-back-main.bat" --from-temp "%~dp0"
  exit /b
)
cd /d "%~2"
echo Back to the normal build. Close the game first.
git fetch origin claude/teyrisphere-performance-hqetcp
git checkout -B claude/teyrisphere-performance-hqetcp origin/claude/teyrisphere-performance-hqetcp
if errorlevel 1 goto failed
powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -SkipDependencies
if errorlevel 1 goto failed
echo.
echo Done: back on the normal build.
pause
exit /b 0
:failed
echo.
echo Something went wrong. Copy the last lines above and send them to Claude.
pause
exit /b 1
