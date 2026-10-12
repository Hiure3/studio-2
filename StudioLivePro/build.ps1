<#
 Studio Live Pro - one-shot build script for Windows 10/11 x64.
 Usage (PowerShell, from the project folder):
     powershell -ExecutionPolicy Bypass -File .\build.ps1 -InstallDeps
 Options:
     -InstallDeps   install missing tools with winget (VS 2022 Build Tools, CMake, Ninja, Inno Setup)
     -AsioSdk <dir> path to the Steinberg ASIO SDK (optional; enables ASIO driver support)
     -SkipTests     skip the automated tests
#>
param([switch]$InstallDeps, [string]$AsioSdk = "", [switch]$SkipTests)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path; Set-Location $root
function Have($c) { return [bool](Get-Command $c -ErrorAction SilentlyContinue) }

# ---- 1. dependencies ----
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVs = (Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if (-not $hasVs -and $InstallDeps) {
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  $hasVs = $true
}
if (-not (Have cmake) -and $InstallDeps) { winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements; $env:Path += ";$env:ProgramFiles\CMake\bin" }
if (-not (Have ninja) -and $InstallDeps) { winget install --id Ninja-build.Ninja -e --accept-source-agreements --accept-package-agreements }
$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc -and $InstallDeps) { winget install --id JRSoftware.InnoSetup -e --accept-source-agreements --accept-package-agreements; $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (-not $hasVs) { throw "Visual Studio 2022 Build Tools (C++ workload) not found. Run with -InstallDeps or install manually." }
if (-not (Have cmake)) { throw "CMake not found. Run with -InstallDeps or install from cmake.org." }

# ---- 2. configure + build (Release, x64, static CRT) ----
$vsPath = & $vswhere -latest -products * -property installationPath
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"; Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$args = @("-S", ".", "-B", "Build\win", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
if ($AsioSdk) { $args += "-DSLP_ASIO_SDK_DIR=$AsioSdk" }
cmake @args; if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build Build\win --config Release 2>&1 | Tee-Object -FilePath Build\build_log.txt; if ($LASTEXITCODE) { throw "Build failed" }

# ---- 3. tests ----
if (-not $SkipTests) {
  $env:SLP_STRESS_SECONDS = "60"; Push-Location Build\win
  & .\slp_tests.exe 2>&1 | Tee-Object -FilePath ..\..\Release\test_output.txt; $testExit = $LASTEXITCODE; Pop-Location
  if ($testExit) { throw "Automated tests FAILED (see Release\test_output.txt)" }
}

# ---- 4. release folder + installer ----
New-Item -ItemType Directory -Force Release | Out-Null
Copy-Item Build\win\StudioLivePro_artefacts\Release\StudioLivePro.exe Release\StudioLivePro.exe -Force
if (-not (Test-Path Release\StudioLivePro.exe)) { Copy-Item (Get-ChildItem -Recurse Build\win -Filter StudioLivePro.exe | Select-Object -First 1).FullName Release\StudioLivePro.exe -Force }
if ($iscc) { & $iscc Installer\StudioLivePro.iss; if ($LASTEXITCODE) { throw "Inno Setup failed" } } else { Write-Warning "Inno Setup not found: installer not generated. Re-run with -InstallDeps." }
Write-Host "`nDONE. Release folder:" (Resolve-Path Release)
