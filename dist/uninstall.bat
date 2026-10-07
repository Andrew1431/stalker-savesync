@echo off
setlocal
title stalker-savesync uninstall
set "BIN=%~dp0..\bin"

if not exist "%BIN%\steam_api64.dll" (
    echo  Can't find your game. This folder must be inside your Anomaly folder.
    goto :end
)
tasklist /fi "imagename eq Anomaly*" 2>nul | "%SystemRoot%\System32\find.exe" /i "Anomaly" >nul && (
    echo  Close the game first, then run uninstall.bat again.
    goto :end
)

findstr /m /c:"StalkerSaveSync_Version" "%BIN%\steam_api64.dll" >nul
if errorlevel 1 (
    rem Already Valve's DLL (e.g. xrRazom update replaced it); just tidy up.
    if exist "%BIN%\steam_api64_real.dll" del "%BIN%\steam_api64_real.dll"
) else (
    if not exist "%BIN%\steam_api64_real.dll" (
        echo  The original steam_api64.dll is missing. Reinstall xrRazom to fix it.
        goto :end
    )
    move /y "%BIN%\steam_api64_real.dll" "%BIN%\steam_api64.dll" >nul || (
        echo  Couldn't restore the original file. Make sure the game is closed.
        goto :end
    )
)
if exist "%BIN%\savesync.log" del "%BIN%\savesync.log"
if exist "%~dp0..\gamedata\scripts\savesync.script" del "%~dp0..\gamedata\scripts\savesync.script"

echo.
echo  Uninstalled. Your saves were not touched.

:end
echo.
pause
