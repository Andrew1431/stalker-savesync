@echo off
rem Builds build\steam_api64.dll (needs Visual Studio 2022+ with C++ tools).
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio C++ build tools not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /LD src\savesync.cpp ^
   /Fobuild\ /Fdbuild\ /Febuild\steam_api64.dll ^
   /link /IMPLIB:build\steam_api64_proxy.lib || exit /b 1
echo Built build\steam_api64.dll
