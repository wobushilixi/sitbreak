@echo off
setlocal
rem ---------------------------------------------------------------
rem  Build SitBreak with MinGW-w64 g++.
rem  Set MINGW_BIN to your toolchain bin dir, or just make sure
rem  g++ is on PATH (MSYS2 / winlibs / Scoop all work).
rem  NOTE: keep this file ASCII-only. cmd.exe reads .bat as ANSI (GBK
rem  on zh-CN), so UTF-8 Chinese comments break line parsing.
rem ---------------------------------------------------------------
if "%MINGW_BIN%"=="" set "MINGW_BIN=D:\tools\mingw\mingw64\bin"
if exist "%MINGW_BIN%\g++.exe" set "PATH=%MINGW_BIN%;%PATH%"

cd /d "%~dp0"
if not exist build mkdir build

rem icon + version info (skipped harmlessly when windres is missing)
where windres >nul 2>nul
if not errorlevel 1 (
  windres -c 65001 -I src -i src\app.rc -O coff -o build\app.res.o
  if errorlevel 1 echo warning: version resource failed, building without it
)

set "RES="
if exist build\app.res.o set "RES=build\app.res.o"

g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    -o "build\SitBreak.exe" src\main.cpp src\app.cpp %RES% ^
    -ld2d1 -ldwrite -ldwmapi -lshell32 -lole32 -luser32 -lgdi32 -luuid -lshlwapi -lwinmm

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK -^> build\SitBreak.exe
