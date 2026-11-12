@echo off
setlocal EnableExtensions
chcp 65001 >nul
title Atlas - build (Windows)

rem ---------------------------------------------------------------------------
rem  Atlas Windows build helper
rem
rem    build.bat             configure + build + run core tests
rem    build.bat --clean     delete build\ first (استفاده بعد از خطای generator)
rem    build.bat --install   install CMake and MSVC Build Tools with winget
rem    build.bat --doctor    show what CMake/toolchains are visible, build nothing
rem    build.bat --run       build, then start the desktop shell
rem
rem  پیام‌ها انگلیسی‌اند چون cmd با UTF-8 همیشه قابل‌اعتماد نیست.
rem  توضیح فارسی و رفع خطاها: tools\windows\README.md
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0..\.."
pushd "%ROOT%" || (echo Cannot open repo root & goto :end_error)

set "ARG=%~1"
if /I "%ARG%"=="--clean" set "DO_CLEAN=1"
if /I "%ARG%"=="--install" set "DO_INSTALL=1"
if /I "%ARG%"=="--run" set "DO_RUN=1"
if /I "%ARG%"=="--doctor" set "DO_DOCTOR=1"

echo.
echo  ============================================
echo   Atlas - Windows build helper
echo  ============================================
echo  repo: %ROOT%
if not "%ROOT%"=="%ROOT:(=%" echo  [i] note: the path contains "(" — if a tool complains, move the repo to C:\atlas
echo.

if defined DO_INSTALL goto :install
goto :find_cmake

:install
if not exist "%~dp0install-msvc.ps1" goto :install_winget_fallback
echo [*] running install-msvc.ps1 (winget force, then the official bootstrapper) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-msvc.ps1"
if errorlevel 1 goto :install_failed
goto :install_cmake

:install_winget_fallback
where winget >nul 2>&1
if errorlevel 1 goto :no_cmake
echo [*] installing CMake and Build Tools with winget ...
winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" --accept-source-agreements --accept-package-agreements

:install_cmake
where cmake >nul 2>&1
if not errorlevel 1 goto :find_cmake
if exist "%ProgramFiles%\CMake\bin\cmake.exe" goto :find_cmake
where winget >nul 2>&1
if errorlevel 1 goto :find_cmake
echo [*] CMake still missing; installing it with winget ...
winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
goto :find_cmake

:install_failed
echo [x] toolchain setup failed; see the messages above.
echo     Smaller alternative ^(no Visual Studio, about 150 MB^):
echo       powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-msvc.ps1" -MinGW
goto :end_error

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

rem ---------------------------------------------------------------------------
rem  انتخاب کامپایلر و ژنراتور — این همان جایی است که «NMake Makefiles» شکست خورد:
rem  CMake روی ویندوز اگر VS نبیند، NMake را انتخاب می‌کند و nmake فقط داخل
rem  Developer Command Prompt وجود دارد. پس ژنراتور را صریح می‌دهیم.
rem ---------------------------------------------------------------------------
call :has_msvc
set "HAVE_GPP="
where g++ >nul 2>&1
if not errorlevel 1 set "HAVE_GPP=1"
set "HAVE_CLANG="
where clang++ >nul 2>&1
if not errorlevel 1 set "HAVE_CLANG=1"
set "HAVE_NINJA="
where ninja >nul 2>&1
if not errorlevel 1 set "HAVE_NINJA=1"
set "HAVE_CL="
where cl >nul 2>&1
if not errorlevel 1 set "HAVE_CL=1"

set "GENERATOR="
set "GEN_ARG="

if defined HAVE_MSVC (
  set "GENERATOR=Visual Studio 17 2022"
  set "GEN_ARG=-A x64"
  goto :gen_ready
)
if defined HAVE_CL (
  if defined HAVE_NINJA (
    set "GENERATOR=Ninja"
  ) else (
    set "GENERATOR=NMake Makefiles"
  )
  goto :gen_ready
)
if defined HAVE_GPP goto :gen_mingw
if defined HAVE_CLANG goto :gen_mingw
goto :gen_ready

:gen_mingw
if defined HAVE_NINJA (
  set "GENERATOR=Ninja"
) else (
  set "GENERATOR=MinGW Makefiles"
)
goto :gen_ready

:gen_ready
if defined DO_DOCTOR goto :doctor

if not defined GENERATOR goto :no_toolchain

echo [*] generator: %GENERATOR% %GEN_ARG%
if /I "%GENERATOR%"=="NMake Makefiles" echo     [i] cl.exe found but ninja is missing; using NMake.

rem اگر build\ با ژنراتور دیگری پیکربندی شده باشد، CMake خطا می‌دهد؛ خودش پاک می‌کنیم.
if defined DO_CLEAN if exist build (
  echo [*] --clean: removing build\
  rmdir /s /q build
)
if not exist "build\CMakeCache.txt" goto :configure
findstr /C:"CMAKE_GENERATOR:INTERNAL=Visual Studio" "build\CMakeCache.txt" >nul 2>&1
if errorlevel 1 if /I not "%GENERATOR%"=="NMake Makefiles" (
  echo [*] removing build\ that was configured with another generator
  rmdir /s /q build
)

:configure
echo [*] configuring ...
call %CMAKE_CMD% -S core -B build -G "%GENERATOR%" %GEN_ARG% -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :configure_failed
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
  dir /b /s build\atlas-cli*.exe 2>nul
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
if defined DO_RUN call "%ROOT%\tools\windows\run-desktop.bat"
goto :end_ok

:doctor
echo.
echo  --- diagnostics ---
where cl 2>nul
where g++ 2>nul
where clang++ 2>nul
where ninja 2>nul
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  echo [*] vswhere found
  "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
) else (
  echo [x] vswhere not found ^(Visual Studio / Build Tools not installed^)
)
echo [*] chosen generator: %GENERATOR% %GEN_ARG%
echo.
echo  If vswhere printed nothing and cl/g++ are absent, install the C++ toolchain:
echo    winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
goto :end_ok

:no_cmake
echo [x] CMake was not found.
echo.
echo   Pick one:
echo     1^) winget install --id Kitware.CMake -e
echo     2^) py -m pip install --upgrade cmake
echo     3^) https://cmake.org/download/  ^(tick "Add CMake to PATH"^)
echo.
echo   Then open a NEW window and run:  tools\windows\build.bat
echo   Or let this script do it:        tools\windows\build.bat --install
goto :end_error

:no_toolchain
echo.
echo [x] No C++ compiler was found (no MSVC, no cl.exe, no g++, no clang++).
echo     CMake therefore fell back to NMake, which only exists inside a
echo     "Developer Command Prompt" — that is the exact error you saw.
echo.
echo     Install the MSVC C++ toolchain ^(2-3 GB, one time^):
echo.
echo       winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo     Then open a NEW PowerShell window and run:  tools\windows\build.bat
echo     If you installed a different compiler ^(MinGW/LLVM^), run:  tools\windows\build.bat --doctor
echo.
goto :end_error

:configure_failed
echo.
echo [x] Configure failed. The CMake output is above.
echo     If it says the generator does not match the cache, run:  tools\windows\build.bat --clean
goto :end_error

:build_failed
echo.
echo [x] Build failed. The compiler output is above.
echo     Common causes: old MSVC without C++20 ^(use VS 2022 17.6+^),
echo     missing "Desktop development with C++" workload, antivirus locking build\.
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

rem ---------------------------------------------------------------------------
rem  زیرروال: آیا MSVC با ابزار C++ نصب است؟
rem ---------------------------------------------------------------------------
:has_msvc
set "HAVE_MSVC="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
  if not "%%i"=="" set "HAVE_MSVC=%%i"
)
goto :eof
