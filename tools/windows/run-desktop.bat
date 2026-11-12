@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Atlas - desktop shell

rem ---------------------------------------------------------------------------
rem  سرور محلی پوستهٔ دسکتاپ را اجرا می‌کند و مرورگر را باز می‌کند.
rem  پیش‌نیاز: یک‌بار tools\windows\build.bat اجرا شده باشد.
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0..\.."
pushd "%ROOT%" || (echo Cannot open repo root & goto :end_error)

set "CLI=build\Release\atlas-cli.exe"
if not exist "%CLI%" set "CLI=build\atlas-cli.exe"
if not exist "%CLI%" (
  echo [x] atlas-cli.exe not found.
  echo     Run tools\windows\build.bat first.
  goto :end_error
)

set "PY_CMD=py"
py -V >nul 2>&1
if errorlevel 1 set "PY_CMD=python"
%PY_CMD% -V >nul 2>&1
if errorlevel 1 (
  echo [x] Python 3 not found. Install it from https://www.python.org/downloads/windows/
  echo     ^(tick "Add python.exe to PATH"^)
  goto :end_error
)

set "PORT=8000"
echo [*] starting Atlas desktop shell on http://localhost:%PORT%
start "Atlas server" %PY_CMD% desktop\server.py --port %PORT%
timeout /t 2 >nul
start "" "http://localhost:%PORT%"
echo [*] browser opened. Close the "Atlas server" window to stop.
popd
endlocal
exit /b 0

:end_error
popd
endlocal
exit /b 1
