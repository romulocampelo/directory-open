@echo off
setlocal
cd /d "%~dp0."
if not exist "%~dp0build\DirOpen.exe" (
  call "%~dp0build.cmd"
  if errorlevel 1 (
    pause
    exit /b 1
  )
)
start "" "%~dp0build\DirOpen.exe"
