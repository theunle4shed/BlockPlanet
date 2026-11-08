@echo off
rem Gets the latest BlockPlanet changes and rebuilds the game. Close the game first.
cd /d "%~dp0"
echo Getting the latest changes...
git pull
if errorlevel 1 goto failed
powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -SkipDependencies
if errorlevel 1 goto failed
echo.
echo Done. Start the game with Play BlockPlanet.bat
pause
exit /b 0
:failed
echo.
echo Something went wrong. Copy the last lines above and send them to Claude.
pause
exit /b 1
