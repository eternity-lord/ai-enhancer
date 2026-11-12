# SPDX-License-Identifier: Apache-2.0
# ---------------------------------------------------------------------------
#  نصب ابزار C++ روی ویندوز برای ساخت Atlas
#
#  سه حالت، به ترتیب تلاش:
#    ۱) winget با --force و workload «VCTools» (سریع‌ترین اگر کار کند)
#    ۲) بوت‌استرپر رسمی Microsoft (https://aka.ms/vs/17/release/vs_buildtools.exe)
#       — این حالت وقتی لازم می‌شود که winget بگوید «نصب است» ولی هیچ نمونهٔ
#       Visual Studio با ابزار C++ وجود نداشته باشد (همان خطای
#       «could not find any instance of Visual Studio»).
#    ۳) راه جایگزین سبک: MinGW-w64 (WinLibs) + Ninja با سوییچ -MinGW
#
#  اجرا:
#    powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1
#    powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1 -MinGW
# ---------------------------------------------------------------------------
[CmdletBinding()]
param(
    [switch]$MinGW,
    [switch]$SkipWinget
)

$ErrorActionPreference = "Stop"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vcComponent = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

function Get-VcInstallPath {
    if (-not (Test-Path $vswhere)) { return $null }
    try {
        $path = & $vswhere -latest -products * -requires $vcComponent -property installationPath 2>$null
    } catch {
        return $null
    }
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    return $path.Trim()
}

function Test-MinGW {
    $gcc = Get-Command g++ -ErrorAction SilentlyContinue
    return [bool]$gcc
}

Write-Host ""
Write-Host "== Atlas toolchain setup ==" -ForegroundColor Cyan
$vc = Get-VcInstallPath
if ($vc) {
    Write-Host "[ok] MSVC with C++ tools is already installed:" -ForegroundColor Green
    Write-Host "     $vc"
    Write-Host "     next: tools\windows\build.bat --clean"
    exit 0
}
Write-Host "[i] no MSVC instance with the C++ workload was found."

# --------------------------- مسیر سبک: MinGW -------------------------------
if ($MinGW) {
    Write-Host "[*] installing MinGW-w64 (WinLibs) and Ninja via winget ..." -ForegroundColor Cyan
    winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e --accept-source-agreements --accept-package-agreements
    winget install --id Ninja-build.Ninja -e --accept-source-agreements --accept-package-agreements
    Write-Host ""
    Write-Host "[i] done. Open a NEW terminal, then run:" -ForegroundColor Yellow
    Write-Host "    tools\windows\build.bat --clean"
    Write-Host "    (the script picks Ninja/MinGW automatically)"
    exit 0
}

# --------------------------- ۱) winget --force ------------------------------
if (-not $SkipWinget) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Host "[*] step 1/2: winget install (force, VCTools workload) ..." -ForegroundColor Cyan
        $override = "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        & winget install --id Microsoft.VisualStudio.2022.BuildTools --force `
            --accept-source-agreements --accept-package-agreements --override $override
        $vc = Get-VcInstallPath
        if ($vc) {
            Write-Host "[ok] MSVC ready: $vc" -ForegroundColor Green
            Write-Host "     next: tools\windows\build.bat --clean"
            exit 0
        }
        Write-Host "[i] winget finished but no C++ tools were found; falling back to the official bootstrapper."
    } else {
        Write-Host "[i] winget not found; skipping step 1."
    }
}

# --------------------------- ۲) بوت‌استرپر رسمی -----------------------------
Write-Host "[*] step 2/2: official Build Tools bootstrapper ..." -ForegroundColor Cyan
$bootstrapper = Join-Path $env:TEMP "vs_buildtools.exe"
$url = "https://aka.ms/vs/17/release/vs_buildtools.exe"
if (-not (Test-Path $bootstrapper)) {
    Write-Host "    downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $bootstrapper -UseBasicParsing
} else {
    Write-Host "    using cached $bootstrapper"
}

Write-Host "    installing the C++ workload — this is a 2-3 GB download, please wait ..." -ForegroundColor Yellow
$vsArgs = @(
    "--quiet", "--wait", "--norestart", "--nocache",
    "--add", "Microsoft.VisualStudio.Workload.VCTools",
    "--includeRecommended"
)
$process = Start-Process -FilePath $bootstrapper -ArgumentList $vsArgs -Wait -PassThru
Write-Host "    installer exit code: $($process.ExitCode)"

$vc = Get-VcInstallPath
if ($vc) {
    Write-Host "[ok] MSVC ready: $vc" -ForegroundColor Green
    Write-Host "     next: tools\windows\build.bat --clean"
    exit 0
}

Write-Host ""
Write-Host "[x] Still no MSVC with the C++ workload." -ForegroundColor Red
Write-Host "    Try one of these:" -ForegroundColor Yellow
Write-Host "      a) open 'Visual Studio Installer' -> Modify -> tick 'Desktop development with C++'"
Write-Host "      b) lighter route (no Visual Studio), MinGW + Ninja:"
Write-Host "         powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1 -MinGW"
if (Test-MinGW) { Write-Host "      c) g++ is already on PATH — run: tools\windows\build.bat --clean" }
exit 1
