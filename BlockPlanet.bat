@echo off
rem BlockPlanet: one menu to play, update, and try other sessions' work.
rem It runs from a copy in TEMP, as switching branch can change this file.
if not "%~1"=="--from-temp" (
  copy /y "%~f0" "%TEMP%\blockplanet-menu.bat" >nul
  "%TEMP%\blockplanet-menu.bat" --from-temp "%~dp0"
  exit /b
)
cd /d "%~2"
set MAIN=main
rem The old name of main, kept on GitHub for a while: not offered as a branch to try.
set OLDMAIN=claude/teyrisphere-performance-hqetcp
:menu
cls
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set CURRENT=%%b
echo  BlockPlanet
echo  ===========
echo  Checking for new changes...
git fetch --prune origin >nul 2>&1
set BEHIND=0
for /f %%n in ('git rev-list --count HEAD..origin/%CURRENT% 2^>nul') do set BEHIND=%%n
cls
echo  BlockPlanet
echo  ===========
if "%CURRENT%"=="%MAIN%" (echo  Build: main) else (echo  Build: %CURRENT%  ^(trying a branch^))
if "%BEHIND%"=="0" (echo  Up to date: choose 1 to play.) else (echo  %BEHIND% new change^(s^) waiting: choose 3 to get them.)
if not "%CURRENT%"=="%MAIN%" echo  Finished trying it? Choose 5 to go back to main.
echo.
echo   1  Play
echo   2  Play with everything unlocked
echo   3  Get the latest changes and rebuild
echo   4  Try a branch (pick from a list)
echo   5  Back to the main build
echo   6  Quit
echo   7  Play with developer tools (F1 opens them in the game)
echo   8  Play classic: the game's own sphere drawing and HUD (to compare)
echo.
set CHOICE=
set /p CHOICE=Choose 1-8: 
if "%CHOICE%"=="1" goto play
if "%CHOICE%"=="2" goto unlocked
if "%CHOICE%"=="3" goto update
if "%CHOICE%"=="4" goto branch
if "%CHOICE%"=="5" goto main
if "%CHOICE%"=="6" exit /b 0
if "%CHOICE%"=="7" goto developer
if "%CHOICE%"=="8" goto classic
goto menu

:play
call :run
goto menu
:unlocked
call :run --unlock-all
goto menu
:developer
call :run --developer
goto menu
:classic
rem The game's own sphere drawing and HUD, for comparing with BlockPlanet's.
set BLOCKPLANET_SPHERE=original
set BLOCKPLANET_ORIGINAL_HUD=1
call :run
set BLOCKPLANET_SPHERE=
set BLOCKPLANET_ORIGINAL_HUD=
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
for /f "tokens=1-3 delims=|" %%a in ('git for-each-ref --sort^=-committerdate "--format=%%(refname:lstrip=3)|%%(committerdate:relative)|%%(contents:subject)" refs/remotes/origin') do (
  if not "%%a"=="HEAD" if not "%%a"=="%MAIN%" if not "%%a"=="%OLDMAIN%" (
    set /a N+=1
    set "B!N!=%%a"
    echo   !N!  %%a   ^(updated %%b^)
    echo        %%c
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
rem Straight back to the options, with the newest copy of this menu (the
rem update may have changed it).
copy /y "BlockPlanet.bat" "%TEMP%\blockplanet-menu.bat" >nul
"%TEMP%\blockplanet-menu.bat" --from-temp "%CD%\"
exit /b

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
