# IW4Iris — assemble the release package from built binaries.
#
#   tools\package.ps1 -Client <iw4x.dll> -Launcher <IW4Iris Launcher.exe>
#
# Contains only what may be redistributed: IW4Iris' own binaries and the
# cinematic build of the IW4x client (GPL-3.0, with its sources available),
# fonts (SIL OFL 1.1), tools and documentation. No game files.
param(
  [Parameter(Mandatory = $true)][string]$Client,
  [Parameter(Mandatory = $true)][string]$Launcher,
  [string]$Version = '0.5.0'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$name = "IW4Iris-v$Version-Windows-x86"
$out = Join-Path $repo "dist\$name"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$out\client", "$out\resources\tools\blender", "$out\resources\fonts", "$out\docs", "$out\samples", "$out\presets" | Out-Null
Copy-Item $Client "$out\client\iw4x.dll"
# Sentry's crash helpers are built with the client and belong next to it.
foreach ($helper in 'sentry-crash.exe', 'sentry-wer.dll') {
  $path = Join-Path (Split-Path -Parent $Client) $helper
  if (Test-Path $path) { Copy-Item $path "$out\client\$helper" }
}
Copy-Item $Launcher "$out\IW4Iris Launcher.exe"
Copy-Item (Join-Path $repo 'resources\fonts\*') "$out\resources\fonts"
Copy-Item (Join-Path $repo 'tools\blender\iw4iris_import.py') "$out\resources\tools\blender"
Copy-Item (Join-Path $repo 'samples\*') "$out\samples" -Recurse
Copy-Item (Join-Path $repo 'presets\*') "$out\presets" -Recurse
New-Item -ItemType Directory -Force -Path "$out\mods" | Out-Null
Copy-Item (Join-Path $repo 'mods\iw4iris_director') "$out\mods" -Recurse
foreach ($doc in 'README.md', 'INSTALL.md', 'TROUBLESHOOTING.md', 'CHANGELOG.md', 'FEATURE_MATRIX.md', 'COMPATIBILITY.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md') {
  Copy-Item (Join-Path $repo $doc) $out
}
Copy-Item (Join-Path $repo 'docs\*') "$out\docs" -Recurse
$zip = Join-Path $repo "dist\$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$out\*" -DestinationPath $zip
Write-Host "info: $zip"
