@echo off
rem BlockPlanet: one menu to play, update, and try other sessions' work.
rem It runs from a copy in TEMP, as switching branch can change this file.
if not "%~1"=="--from-temp" (
  copy /y "%~f0" "%TEMP%\blockplanet-menu.bat" >nul
  "%TEMP%\blockplanet-menu.bat" --from-temp "%~dp0"
  exit /b
)
cd /d "%~2"
set MAIN=claude/teyrisphere-performance-hqetcp
:menu
cls
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set CURRENT=%%b
echo  BlockPlanet
echo  ===========
if "%CURRENT%"=="%MAIN%" (echo  Build: main) else (echo  Build: %CURRENT%  ^(trying a branch^))
echo.
echo   1  Play
echo   2  Play with everything unlocked
echo   3  Update and rebuild
echo   4  Try a branch (pick from a list)
echo   5  Back to the main build
echo   6  Quit
echo.
set CHOICE=
set /p CHOICE=Choose 1-6: 
if "%CHOICE%"=="1" goto play
if "%CHOICE%"=="2" goto unlocked
if "%CHOICE%"=="3" goto update
if "%CHOICE%"=="4" goto branch
if "%CHOICE%"=="5" goto main
if "%CHOICE%"=="6" exit /b 0
goto menu

:play
call :run
goto menu
:unlocked
call :run --unlock-all
goto menu

:update
echo Close the game first. Getting the latest changes...
git pull
if errorlevel 1 goto failed
goto build

:branch
cls
echo  Try a branch (another session's work). Close the game first.
echo  Looking up the branches...
git fetch --prune origin >nul 2>&1
setlocal enabledelayedexpansion
set N=0
for /f "delims=" %%r in ('git for-each-ref --sort^=-committerdate --format^=%%^(refname:lstrip^=3^) refs/remotes/origin') do (
  if not "%%r"=="HEAD" if not "%%r"=="%MAIN%" (
    set /a N+=1
    set "B!N!=%%r"
    echo   !N!  %%r
  )
)
echo   0  Back
echo.
set PICK=
set /p PICK=Choose a branch: 
if "%PICK%"=="" (endlocal & goto menu)
if "%PICK%"=="0" (endlocal & goto menu)
set "BRANCH=!B%PICK%!"
if "!BRANCH!"=="" (endlocal & goto branch)
endlocal & set "BRANCH=%BRANCH%"
echo Switching to %BRANCH%...
git fetch origin %BRANCH%
if errorlevel 1 goto failed
git checkout -B %BRANCH% origin/%BRANCH%
if errorlevel 1 goto failed
goto build

:main
echo Close the game first. Back to the main build...
git fetch origin %MAIN%
git checkout -B %MAIN% origin/%MAIN%
if errorlevel 1 goto failed
goto build

:build
powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -SkipDependencies
if errorlevel 1 goto failed
echo.
echo Done.
pause
goto menu

:run
if not exist ".local\m1\build-windows\Release\tetrisphere-m1.exe" (
  echo BlockPlanet is not built yet. Choose 3 first.
  pause
  exit /b 1
)
pushd ".local\m1\build-windows\Release"
tetrisphere-m1.exe %*
if errorlevel 1 (
  echo.
  echo The game stopped with an error. If it says an option is unknown, choose 3 first.
  pause
)
popd
exit /b 0

:failed
echo.
echo Something went wrong. Copy the last lines above and send them to Claude.
pause
goto menu
