@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Atlas - build Atlas.exe

rem ---------------------------------------------------------------------------
rem  ساخت برنامهٔ واقعی ویندوز: desktop\dist\Atlas.exe
rem  پیش‌نیاز: یک‌بار tools\windows\build.bat (برای ساخت atlas-cli.exe)
rem  خروجی یک فایل است و داخلش رابط شیشه‌ای + atlas-cli.exe هست.
rem  پیام‌ها انگلیسی‌اند چون cmd با UTF-8 همیشه قابل‌اعتماد نیست؛ توضیح فارسی:
rem    desktop\README.md  (بخش «برنامهٔ واقعی»)
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0.."
pushd "%ROOT%\desktop" || (echo Cannot open desktop folder & goto :end_error)

set "PY=py"
py -V >nul 2>&1
if errorlevel 1 set "PY=python"
%PY% -V >nul 2>&1
if errorlevel 1 (
  echo [x] Python 3 not found. Install it from https://www.python.org/downloads/windows/
  echo     ^(tick "Add python.exe to PATH"^)
  goto :end_error
)

if not exist "..\build\atlas-cli.exe" (
  echo [x] build\atlas-cli.exe not found.
  echo     First run: tools\windows\build.bat
  goto :end_error
)

echo [*] installing build dependencies: pywebview, pyinstaller ...
%PY% -m pip install --upgrade pywebview pyinstaller
if errorlevel 1 (
  echo [x] pip install failed. Check your internet connection or proxy.
  goto :end_error
)

echo [*] packaging Atlas.exe (this takes a couple of minutes) ...
%PY% -m PyInstaller --noconfirm --clean --onefile --noconsole --name Atlas ^
  --add-data "web;web" ^
  --add-binary "..\build\atlas-cli.exe;." ^
  app.py
if errorlevel 1 (
  echo [x] PyInstaller failed. The output above says why.
  goto :end_error
)

if not exist "dist\Atlas.exe" (
  echo [x] dist\Atlas.exe was not produced.
  goto :end_error
)

echo.
echo  ============================================
echo   done:  %ROOT%\desktop\dist\Atlas.exe
echo  ============================================
echo   double-click it. First start takes a few seconds.
echo   log file: %%LOCALAPPDATA%%\Atlas\data\atlas.log
echo.
popd
endlocal
exit /b 0

:end_error
popd
endlocal
exit /b 1
