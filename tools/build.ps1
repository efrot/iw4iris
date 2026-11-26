# IW4Iris — Windows build (Visual Studio 2022 with the C++ desktop workload).
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Configuration Release] [-Package]
#
# 1. checks out the pinned IW4x client into build\iw4x-cinematic,
# 2. installs the IW4Iris component (tools\iw4x-overlay.ps1),
# 3. generates and builds IW4x the way IW4x's CI does (premake5 vs2022, MSBuild),
# 4. builds the launcher and the tests with CMake and runs the tests,
# 5. with -Package, assembles dist\IW4Iris-v<version>-Windows-x86.zip.
param(
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  [switch]$Package,
  [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Run([string]$exe, [string[]]$arguments) {
  & $exe @arguments
  if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed ($LASTEXITCODE)" }
}

Run git @('submodule', 'update', '--init', '--recursive')

$manifest = Get-Content 'compatibility\manifest.json' -Raw | ConvertFrom-Json
$iw4x = Join-Path $repo 'build\iw4x-cinematic'
if (-not (Test-Path (Join-Path $iw4x '.git'))) {
  Run git @('clone', '--no-checkout', (Join-Path $repo 'external\iw4x-client'), $iw4x)
}
Run git @('-C', $iw4x, 'checkout', '--force', $manifest.iw4x.commit)
Run git @('-C', $iw4x, 'remote', 'set-url', 'origin', $manifest.iw4x.repository)
Run git @('-C', $iw4x, 'submodule', 'update', '--init', '--recursive')

& (Join-Path $PSScriptRoot 'iw4x-overlay.ps1') -Iw4x $iw4x

Push-Location $iw4x
try {
  Run '.\tools\premake5.exe' @('vs2022')
  $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
  if (-not $msbuild) { throw 'MSBuild (Visual Studio 2022) was not found' }
  Run $msbuild @('/m', '/v:minimal', "/p:Configuration=$Configuration", '/p:Platform=Win32', 'build\iw4x.sln')
} finally {
  Pop-Location
}
$client = Join-Path $iw4x "build\bin\Win32\$Configuration\iw4x.dll"
Write-Host "info: built $client"

Run cmake @('-S', '.', '-B', 'build\cmake-win32', '-A', 'Win32', '-DIW4IRIS_BUILD_LAUNCHER=ON')
Run cmake @('--build', 'build\cmake-win32', '--config', $Configuration, '--parallel')
if (-not $SkipTests) {
  Run ctest @('--test-dir', 'build\cmake-win32', '-C', $Configuration, '--output-on-failure')
  # "python" can be the Microsoft Store placeholder on a fresh Windows; the
  # py launcher (python.org installer) finds a real Python 3.
  $python = @('python')
  if (Get-Command py -ErrorAction SilentlyContinue) { $python = @('py', '-3') }
  Run $python[0] (@($python | Select-Object -Skip 1) + @('-I', 'tests\python\test_blender_addon.py'))
  Run $python[0] (@($python | Select-Object -Skip 1) + @('-I', 'tests\python\test_gsc.py'))
}

if ($Package) {
  & (Join-Path $PSScriptRoot 'package.ps1') -Client $client -Launcher "build\cmake-win32\launcher\$Configuration\IW4Iris Launcher.exe"
}
