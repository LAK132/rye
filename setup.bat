@echo off
SetLocal EnableDelayedExpansion

set meson_args=

@REM if "%~1"=="wasm32" (
@REM   shift
@REM   set cross_args="--cross-file=cross/emscripten.txt --cross-file=cross/wasm32.txt"
@REM   goto build-compiler
@REM )

@REM if "%~1"=="wasm64" (
@REM   shift
@REM   set cross_args="--cross-file=cross/emscripten.txt --cross-file=cross/wasm64.txt"
@REM   goto build-compiler
@REM )

if "%~1"=="clang" (
  set CC=clang
  set CXX=clang++
  set cross_args=
  goto build-compiler
)

if "%~1"=="gcc" (
  set CC=gcc
  set CXX=g++
  set cross_args=
  goto build-compiler
)

set cross_args=

:build-compiler
if "%~1"=="msvc" (
  shift
  set meson_args=!meson_args! --vsenv
  goto run
)

if "%~1"=="clang" (
  shift
  set CC_FOR_BUILD=clang
  set CXX_FOR_BUILD=clang++
  goto run
)

if "%~1"=="gcc" (
  shift
  set CC_FOR_BUILD=gcc
  set CXX_FOR_BUILD=g++
  goto run
)

goto usage

:run

:arg-loop
if "%~1"=="" goto end-arg-loop
set meson_args=!meson_args! "%~1"
shift
goto arg-loop
:end-arg-loop

meson setup --wipe !cross_args! build !meson_args!
goto :eof

:usage
echo setup.bat ^<cross target^> [native compiler] ^<setup args^>
echo examples:
echo setup.bat msvc
echo setup.bat msvc "--buildtype=release" "-Dbindir=bin"
echo setup.bat gcc "--buildtype=debug"
echo setup.bat clang "--buildtype=debugoptimized"
exit /b 1
