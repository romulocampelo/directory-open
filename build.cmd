@echo off
setlocal
rem Build only with existing tools; keep environment changes local.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "NAVEGA_PREVIOUS_CP=%%C"
chcp 65001 >nul
set "NAVEGA_FRESH="
set "NAVEGA_BUILD_DIR=%~dp0build"
:arguments
if "%~1"=="" goto arguments_done
if "%~1"=="--fresh" (
  set "NAVEGA_FRESH=--fresh"
  shift /1
  goto arguments
)
if "%~1"=="--build-dir" (
  if "%~2"=="" goto usage
  set "NAVEGA_BUILD_DIR=%~f2"
  shift /1
  shift /1
  goto arguments
)
goto usage
:usage
echo Uso: build.cmd [--fresh] [--build-dir "PASTA-DENTRO-DO-REPOSITORIO"]
goto failed
:arguments_done
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0scripts\check-build-directory.ps1" -Root "%~dp0." -BuildDirectory "%NAVEGA_BUILD_DIR%"
if not "%errorlevel%"=="0" goto failed
if not defined NAVEGA_QT_DIR if defined QT_ROOT_DIR set "NAVEGA_QT_DIR=%QT_ROOT_DIR%"
if not defined NAVEGA_QT_DIR set "NAVEGA_QT_DIR=C:\Qt\6.12.0\msvc2022_64"
if not exist "%NAVEGA_QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo Qt MSVC x64 nao encontrado. Defina NAVEGA_QT_DIR com a pasta do Qt.
  goto failed
)
if not defined NAVEGA_VS_DIR (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "NAVEGA_VS_DIR=%%I"
  )
)
if not exist "%NAVEGA_VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" (
  echo Visual Studio com ferramentas C++ x64 nao encontrado. Defina NAVEGA_VS_DIR.
  goto failed
)
rem Prefer the locally validated compiler when installed; honor explicit choices.
if not defined NAVEGA_MSVC_VERSION if exist "%NAVEGA_VS_DIR%\VC\Tools\MSVC\14.44.*" set "NAVEGA_MSVC_VERSION=14.44"
set "VSLANG=1033"
if defined NAVEGA_MSVC_VERSION (
  call "%NAVEGA_VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=%NAVEGA_MSVC_VERSION%
) else (
  call "%NAVEGA_VS_DIR%\VC\Auxiliary\Build\vcvars64.bat"
)
if not "%errorlevel%"=="0" goto failed
set "PATH=%NAVEGA_QT_DIR%\bin;%PATH%"
if not defined NAVEGA_NINJA if exist "C:\Qt\Tools\Ninja\ninja.exe" set "NAVEGA_NINJA=C:\Qt\Tools\Ninja\ninja.exe"
if not defined NAVEGA_NINJA for /f "delims=" %%N in ('where ninja.exe 2^>nul') do if not defined NAVEGA_NINJA set "NAVEGA_NINJA=%%N"
if not exist "%NAVEGA_NINJA%" (
  echo Ninja nao encontrado. Defina NAVEGA_NINJA com o caminho de ninja.exe.
  goto failed
)
where cmake.exe >nul 2>nul
if not "%errorlevel%"=="0" (
  echo CMake nao encontrado. Instale CMake 3.21 ou superior e abra um novo terminal.
  goto failed
)
cmake %NAVEGA_FRESH% -S "%~dp0." -B "%NAVEGA_BUILD_DIR%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NAVEGA_NINJA%" -DCMAKE_PREFIX_PATH="%NAVEGA_QT_DIR%" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDIROPEN_PRESENTATION_PROBE=OFF
if not "%errorlevel%"=="0" goto failed
cmake --build "%NAVEGA_BUILD_DIR%" --parallel 2
if not "%errorlevel%"=="0" goto failed
ctest --test-dir "%NAVEGA_BUILD_DIR%" --output-on-failure
if not "%errorlevel%"=="0" goto failed
"%NAVEGA_QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime "%NAVEGA_BUILD_DIR%\DirOpen.exe"
if not "%errorlevel%"=="0" goto failed
set "NAVEGA_RESULT=0"
goto finish
:failed
set "NAVEGA_RESULT=1"
:finish
if defined NAVEGA_PREVIOUS_CP chcp %NAVEGA_PREVIOUS_CP% >nul
exit /b %NAVEGA_RESULT%
