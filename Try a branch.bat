@echo off
rem Tries another session's work (a branch), then rebuilds. "Back to main build.bat" returns.
rem It runs from a copy in TEMP, as switching branch can change or remove this file.
if not "%~1"=="--from-temp" (
  copy /y "%~f0" "%TEMP%\blockplanet-try-branch.bat" >nul
  "%TEMP%\blockplanet-try-branch.bat" --from-temp "%~dp0"
  exit /b
)
cd /d "%~2"
echo Branches you can try (menu-grid, direct-launch, ...). Close the game first.
set /p BRANCH=Branch name: 
if "%BRANCH%"=="" set BRANCH=claude/teyrisphere-performance-hqetcp
echo Switching to %BRANCH%...
git fetch origin %BRANCH%
if errorlevel 1 goto failed
git checkout -B %BRANCH% origin/%BRANCH%
if errorlevel 1 goto failed
powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -SkipDependencies
if errorlevel 1 goto failed
echo.
echo Done: you are on %BRANCH%. Play with Play BlockPlanet.bat. Back to your normal build: Back to main build.bat
pause
exit /b 0
:failed
echo.
echo Something went wrong. Copy the last lines above and send them to Claude.
pause
exit /b 1
