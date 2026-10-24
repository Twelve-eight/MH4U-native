@echo off
rem Builds mh4uprobe.dll, the read-only M0 site probe for Forge PC (MSVC x64, C only).
rem forge_pc.h: set FORGE_INCLUDE, else the workspace checkout ..\..\forge-pc-zh\include is used.
rem Output: dist\mods\0100770008DD8000\MH4U\plugins\mh4uprobe.dll
setlocal
cd /d "%~dp0"

where cl >nul 2>&1 && goto :env_ok
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath > "%TEMP%\mh4uprobe_vsdir.txt"
set /p VSDIR=<"%TEMP%\mh4uprobe_vsdir.txt"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:env_ok

if not defined FORGE_INCLUDE set "FORGE_INCLUDE=..\..\forge-pc-zh\include"
set "OUT=dist\mods\0100770008DD8000\MH4U\plugins"
if not exist build mkdir build
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /O2 /MT /W4 /LD mh4uprobe.c /I"%FORGE_INCLUDE%" /Fo:build\mh4uprobe.obj /Fe:"%OUT%\mh4uprobe.dll" /link /IMPLIB:build\mh4uprobe.lib || exit /b 1
del /q "%OUT%\mh4uprobe.exp" 2>nul

echo.
echo Built: %OUT%\mh4uprobe.dll
