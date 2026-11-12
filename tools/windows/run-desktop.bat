@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Atlas - desktop app

rem ---------------------------------------------------------------------------
rem  پنجرهٔ نیتیو Atlas را *بدون* ساخت exe باز می‌کند (برای توسعه/آزمایش).
rem  پیش‌نیاز: یک‌بار tools\windows\build.bat برای ساخت atlas-cli.exe.
rem  اگر pywebview نصب نباشد، همان اجرا نصبش می‌کند؛ اگر پنجره باز نشد،
rem  به مرورگر برمی‌گردد تا هیچ‌وقت دست خالی نمانید.
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

%PY_CMD% -c "import webview" >nul 2>&1
if errorlevel 1 (
  echo [*] installing pywebview ...
  %PY_CMD% -m pip install --quiet --upgrade pywebview
)

echo [*] opening the Atlas window ...
%PY_CMD% desktop\app.py
if errorlevel 1 (
  echo [!] native window failed - starting the local browser shell instead.
  set "PORT=8000"
  start "Atlas server" %PY_CMD% desktop\server.py --port %PORT%
  timeout /t 2 >nul
  start "" "http://localhost:%PORT%"
  echo [*] browser opened. Close the "Atlas server" window to stop.
)

popd
endlocal
exit /b 0

:end_error
popd
endlocal
exit /b 1
