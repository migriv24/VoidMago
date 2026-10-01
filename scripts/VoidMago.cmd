@echo off
title Void Mago Desktop
rem Launcher for Void Mago Desktop, and the fallback that shows you why it did
rem not start. The desktop shortcut points here rather than at the binary so a
rem missing build or a missing libvoidcore.dll produces a sentence instead of a
rem window that never appears.
setlocal

set "REPO=%~dp0.."
set "EXE=%REPO%\build-desktop\bin\voidmago-desktop.exe"
if not exist "%EXE%" set "EXE=%REPO%\build\bin\voidmago-desktop.exe"

if not exist "%EXE%" (
  echo Void Mago Desktop is not built.
  echo.
  echo   cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOIDMAGO_BUILD_DESKTOP=ON
  echo   cmake --build build-desktop
  echo.
  echo The CLI needs none of that: `cmake -S . -B build ^&^& cmake --build build`
  echo builds `mago` with no dependencies at all.
  echo.
  pause
  exit /b 1
)

start "" "%EXE%"
endlocal
