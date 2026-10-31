@echo off
REM Builds a target with the MSYS2 clang64 toolchain (clang + libc++ + mingw-w64).
REM
REM   build.cmd                          debug
REM   build.cmd cmake-build-release      release
REM   build.cmd cmake-build-mingw shaders
REM
REM Configures the directory first when it has no cache

setlocal enabledelayedexpansion
set "TOOLCHAIN=C:\msys64\clang64\bin"
set "PATH=%TOOLCHAIN%;%USERPROFILE%\.cmake-deps\ninja\win\x64;%PATH%"

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=cmake-build-mingw"
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=app"

if not exist "%~dp0%BUILD_DIR%\CMakeCache.txt" (
  if /i "%BUILD_DIR%"=="cmake-build-release" (set "CFG=Release") else (set "CFG=Debug")
  cmake -S "%~dp0." -B "%~dp0%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=!CFG! ^
        -DCMAKE_C_COMPILER="%TOOLCHAIN%\clang.exe" -DCMAKE_CXX_COMPILER="%TOOLCHAIN%\clang++.exe" || exit /b 1
)

cmake --build "%~dp0%BUILD_DIR%" --target %TARGET%
