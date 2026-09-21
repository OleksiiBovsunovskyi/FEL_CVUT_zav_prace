@echo off
REM Builds a target under the VS 2022 BuildTools environment (MSVC 19.44), the
REM toolchain the Ninja + Clang build directories were configured with.
REM
REM   build.cmd                          debug 
REM   build.cmd cmake-build-release      release
REM   build.cmd cmake-build-debug shaders
REM
REM Configures the directory first when it has no cache

setlocal enabledelayedexpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=cmake-build-debug"
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=pgr-vk"

if not exist "%~dp0%BUILD_DIR%\CMakeCache.txt" (
  if /i "%BUILD_DIR%"=="cmake-build-release" (set "CFG=Release") else (set "CFG=Debug")
  cmake -S "%~dp0." -B "%~dp0%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=!CFG! ^
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
)

cmake --build "%~dp0%BUILD_DIR%" --target %TARGET%
