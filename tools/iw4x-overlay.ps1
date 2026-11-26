# IW4Iris — install the IW4Iris component into an IW4x client checkout.
#
#   powershell -ExecutionPolicy Bypass -File tools\iw4x-overlay.ps1 -Iw4x <iw4x-client-dir>
#
# Applies engine\iw4x\patches\*.patch (skipped when already applied), copies
# the component to src\Components\Modules\IW4Iris\ and the editor libraries
# to deps\iw4iris\ (see tools/iw4x-overlay.sh for the same on Linux).
param([Parameter(Mandatory = $true)][string]$Iw4x)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$dst = (Resolve-Path $Iw4x).Path
$manifest = Get-Content (Join-Path $repo 'compatibility\manifest.json') -Raw | ConvertFrom-Json
$actual = (git -C $dst rev-parse HEAD).Trim()
if ($actual -ne $manifest.iw4x.commit) {
  Write-Warning "$dst is at $actual, IW4Iris is verified against $($manifest.iw4x.commit)"
}

# "git apply --reverse --check" reports a patch that is not applied yet on
# stderr. Windows PowerShell 5.1 turns redirected native stderr into an
# error record, which 'Stop' makes fatal (PowerShell 7 does not), so the
# check runs with 'Continue' and only its exit code counts.
function Test-PatchApplied([string]$patchFile) {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    git -C $dst apply --reverse --check --ignore-whitespace $patchFile 2>&1 | Out-Null
    return $LASTEXITCODE -eq 0
  } finally {
    $ErrorActionPreference = $previous
  }
}

foreach ($patch in Get-ChildItem (Join-Path $repo 'engine\iw4x\patches\*.patch')) {
  if (Test-PatchApplied $patch.FullName) {
    Write-Host "info: $($patch.Name) already applied"
  } else {
    git -C $dst apply --ignore-whitespace $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "could not apply $($patch.Name)" }
    Write-Host "info: applied $($patch.Name)"
  }
}

function Sync-Folder([string]$from, [string]$to) {
  # robocopy /MIR keeps unchanged files (timestamps) so builds stay incremental.
  New-Item -ItemType Directory -Force -Path $to | Out-Null
  robocopy $from $to /MIR /NJH /NJS /NFL /NDL /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy $from -> $to failed ($LASTEXITCODE)" }
  $global:LASTEXITCODE = 0
}

Sync-Folder (Join-Path $repo 'engine\iw4x\src') (Join-Path $dst 'src\Components\Modules\IW4Iris')

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("iw4iris-" + [guid]::NewGuid())
try {
  New-Item -ItemType Directory -Force -Path "$stage\core", "$stage\imgui\backends", "$stage\imgui\misc\cpp", "$stage\stb", "$stage\nanosvg", "$stage\earcut\mapbox" | Out-Null
  Copy-Item -Recurse (Join-Path $repo 'core\include'), (Join-Path $repo 'core\src') "$stage\core"
  Copy-Item (Join-Path $repo 'third_party\imgui\*.cpp'), (Join-Path $repo 'third_party\imgui\*.h'), (Join-Path $repo 'third_party\imgui\LICENSE.txt') "$stage\imgui"
  Copy-Item (Join-Path $repo 'third_party\imgui\backends\imgui_impl_dx9.*'), (Join-Path $repo 'third_party\imgui\backends\imgui_impl_win32.*') "$stage\imgui\backends"
  Copy-Item (Join-Path $repo 'third_party\imgui\misc\cpp\imgui_stdlib.*') "$stage\imgui\misc\cpp"
  Copy-Item (Join-Path $repo 'third_party\stb\stb_image_write.h') "$stage\stb"
  Copy-Item (Join-Path $repo 'third_party\stb\stb_truetype.h') "$stage\stb"
  Copy-Item (Join-Path $repo 'third_party\stb\stb_image.h') "$stage\stb"
  Copy-Item (Join-Path $repo 'third_party\nanosvg\src\nanosvg.h') "$stage\nanosvg"
  Copy-Item (Join-Path $repo 'third_party\nanosvg\LICENSE.txt') "$stage\nanosvg"
  Copy-Item (Join-Path $repo 'third_party\earcut\include\mapbox\earcut.hpp') "$stage\earcut\mapbox"
  Copy-Item (Join-Path $repo 'third_party\earcut\LICENSE') "$stage\earcut"
  Copy-Item (Join-Path $repo 'engine\iw4x\premake\stb_impl.cpp') "$stage\stb_impl.cpp"
  Sync-Folder $stage (Join-Path $dst 'deps\iw4iris')
} finally {
  Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}
Copy-Item (Join-Path $repo 'engine\iw4x\premake\iw4iris.lua') (Join-Path $dst 'deps\premake\iw4iris.lua') -Force
Write-Host "info: IW4Iris installed into $dst"
