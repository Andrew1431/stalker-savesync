@echo off
rem Builds and zips the release: build\stalker-savesync.zip
setlocal
cd /d "%~dp0"
call "%~dp0build.bat" || exit /b 1

set "STAGE=build\stage\stalker-savesync"
if exist build\stage rmdir /s /q build\stage
mkdir "%STAGE%"
copy /y build\steam_api64.dll "%STAGE%\savesync.dll" >nul
copy /y dist\install.bat "%STAGE%\" >nul
copy /y dist\uninstall.bat "%STAGE%\" >nul
copy /y lua\gamedata\scripts\savesync.script "%STAGE%\" >nul
copy /y README.md "%STAGE%\README.txt" >nul

if exist build\stalker-savesync.zip del build\stalker-savesync.zip
powershell -NoProfile -Command "Compress-Archive -Path 'build\stage\stalker-savesync' -DestinationPath 'build\stalker-savesync.zip'" || exit /b 1
echo Packaged build\stalker-savesync.zip
