@echo off
rem Starts BlockPlanet. Double-click to play.
cd /d "%~dp0.local\m1\build-windows\Release"
if not exist tetrisphere-m1.exe (
  echo BlockPlanet is not built yet. Run Update BlockPlanet.bat first.
  pause
  exit /b 1
)
tetrisphere-m1.exe %*
