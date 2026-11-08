@echo off
setlocal
rem ---------------------------------------------------------------
rem  Build SitBreak with MinGW-w64 g++.
rem  Set MINGW_BIN to your toolchain bin dir, or just make sure
rem  g++ is on PATH (MSYS2 / winlibs / Scoop all work).
rem ---------------------------------------------------------------
if "%MINGW_BIN%"=="" set "MINGW_BIN=D:\tools\mingw\mingw64\bin"
if exist "%MINGW_BIN%\g++.exe" set "PATH=%MINGW_BIN%;%PATH%"

cd /d "%~dp0"
if not exist build mkdir build

g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    -o "build\SitBreak.exe" src\main.cpp src\app.cpp ^
    -ld2d1 -ldwrite -ldwmapi -lshell32 -lole32 -luser32 -lgdi32 -luuid -lshlwapi -lwinmm

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK -^> build\SitBreak.exe
