@echo off
setlocal
cd /d "%~dp0."
rem Latest locally validated runtime; keep this path current with each delivery.
set "DIROPEN_EXECUTABLE=%~dp0build-publicacao024\DirOpen.exe"
if not exist "%DIROPEN_EXECUTABLE%" (
  rem A source checkout must build current sources, not open an older fallback.
  call "%~dp0build.cmd"
  if errorlevel 1 (
    pause
    exit /b 1
  )
  set "DIROPEN_EXECUTABLE=%~dp0build\DirOpen.exe"
)
start "" "%DIROPEN_EXECUTABLE%" %*
exit /b %errorlevel%
