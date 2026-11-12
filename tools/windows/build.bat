@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Atlas - build (Windows)

rem ---------------------------------------------------------------------------
rem  Atlas Windows build helper
rem  Usage:
rem    build.bat                 -> configure + build + run core tests
rem    build.bat --install       -> install CMake with winget first, then build
rem    build.bat --run           -> build, then start the desktop shell
rem  پیام‌ها انگلیسی‌اند چون cmd با UTF-8 همیشه قابل‌اعتماد نیست؛ توضیح فارسی:
rem    tools\windows\README.md
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0..\.."
pushd "%ROOT%" || (echo Cannot open repo root & goto :end)

echo.
echo  ============================================
echo   Atlas - Windows build helper
echo  ============================================
echo  repo: %ROOT%
echo.

if /I "%~1"=="--install" goto :install
goto :find_cmake

:install
where winget >nul 2>&1
if errorlevel 1 (
  echo [x] winget not found. Install CMake manually or with pip:
  echo     py -m pip install --upgrade cmake
  goto :end_error
)
echo [*] installing CMake with winget ...
winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
echo.

:find_cmake
set "CMAKE_CMD="
where cmake >nul 2>&1
if not errorlevel 1 set "CMAKE_CMD=cmake"
if not defined CMAKE_CMD if exist "%ProgramFiles%\CMake\bin\cmake.exe" set CMAKE_CMD="%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_CMD if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set CMAKE_CMD="%ProgramFiles(x86)%\CMake\bin\cmake.exe"
if not defined CMAKE_CMD if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set CMAKE_CMD="%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"

if not defined CMAKE_CMD (
  py -m cmake --version >nul 2>&1
  if not errorlevel 1 set "CMAKE_CMD=py -m cmake"
)
if not defined CMAKE_CMD (
  python -m cmake --version >nul 2>&1
  if not errorlevel 1 set "CMAKE_CMD=python -m cmake"
)

if not defined CMAKE_CMD goto :no_cmake

echo [*] cmake: %CMAKE_CMD%
call %CMAKE_CMD% --version
echo.

echo [*] configuring (Visual Studio generator if available) ...
call %CMAKE_CMD% -S core -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :no_toolchain
echo.

echo [*] building ...
call %CMAKE_CMD% --build build --config Release -j
if errorlevel 1 goto :build_failed
echo.

set "CLI=build\Release\atlas-cli.exe"
if not exist "%CLI%" set "CLI=build\atlas-cli.exe"
set "TESTS=build\Release\atlas-tests.exe"
if not exist "%TESTS%" set "TESTS=build\atlas-tests.exe"

if not exist "%CLI%" (
  echo [x] build finished but atlas-cli.exe was not found in build\ or build\Release\
  goto :end_error
)

echo [*] running core tests ...
"%TESTS%"
if errorlevel 1 goto :tests_failed

echo.
echo  ============================================
echo   done.  atlas-cli = %CLI%
echo  ============================================
echo   next:  tools\windows\run-desktop.bat
echo   or:    %CLI% --help
echo.

if /I "%~1"=="--run" call "%ROOT%\tools\windows\run-desktop.bat"
goto :end_ok

:no_cmake
echo [x] CMake was not found.
echo.
echo   Pick one:
echo     1^) winget install --id Kitware.CMake -e
echo     2^) py -m pip install --upgrade cmake
echo     3^) download from https://cmake.org/download/  (tick "Add CMake to PATH")
echo.
echo   Then close this window, open a NEW one, and run:  tools\windows\build.bat
echo   Or let this script do it:                        tools\windows\build.bat --install
goto :end_error

:no_toolchain
echo.
echo [x] No C++ compiler was found by CMake.
echo     Install "Visual Studio Build Tools" with the "Desktop development with C++"
echo     workload, then run this script again in a NEW window:
echo.
echo       winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools"
echo.
goto :end_error

:build_failed
echo.
echo [x] Build failed. The compiler output is above.
echo     Common causes: an old MSVC that lacks C++20 (use VS 2022 17.6+),
echo     a missing "Desktop development with C++" workload, or antivirus locking build\.
goto :end_error

:tests_failed
echo.
echo [x] Core tests failed. Please paste the output above in the issue.
goto :end_error

:end_ok
popd
endlocal
exit /b 0

:end_error
popd
endlocal
exit /b 1
