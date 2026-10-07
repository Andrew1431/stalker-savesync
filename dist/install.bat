@echo off
setlocal
title stalker-savesync install
set "HERE=%~dp0"
set "BIN=%HERE%..\bin"
set "PROXY=%HERE%savesync.dll"

if not exist "%BIN%\steam_api64.dll" (
    echo.
    echo  Can't find your game. Put this "stalker-savesync" folder INSIDE your
    echo  Anomaly folder ^(the one that has "bin" and "gamedata" in it^), then
    echo  run install.bat again.
    goto :end
)
if not exist "%PROXY%" (
    echo  savesync.dll is missing from this folder. Download the zip again.
    goto :end
)
tasklist /fi "imagename eq Anomaly*" 2>nul | "%SystemRoot%\System32\find.exe" /i "Anomaly" >nul && (
    echo  Close the game first, then run install.bat again.
    goto :end
)

findstr /m /c:"StalkerSaveSync_Version" "%BIN%\steam_api64.dll" >nul
if errorlevel 1 (
    rem Valve's DLL is in place: fresh install, or xrRazom was updated.
    copy /y "%BIN%\steam_api64.dll" "%BIN%\steam_api64_real.dll" >nul || goto :fail
) else (
    if not exist "%BIN%\steam_api64_real.dll" (
        echo  The original steam_api64.dll is missing. Reinstall xrRazom, then run this again.
        goto :end
    )
)
copy /y "%PROXY%" "%BIN%\steam_api64.dll" >nul || goto :fail
if not exist "%HERE%..\gamedata\scripts" mkdir "%HERE%..\gamedata\scripts"
copy /y "%HERE%savesync.script" "%HERE%..\gamedata\scripts\savesync.script" >nul || goto :fail

echo.
echo  Installed! Start the game like normal.
goto :end

:fail
echo  Something went wrong copying files. Make sure the game is closed and try again.

:end
echo.
pause
