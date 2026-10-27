# Builds an update package for the in-app updater.
#   1. Bump VERSION in the root CMakeLists.txt (project(... VERSION x.y.z)) and rebuild.
#   2. Run this script.
#   3. Upload dist\RareStudio-<ver>.zip and dist\latest.json to wherever "update/url" points
#      (e.g. a GitHub release, with latest.json's "url" pointing at the uploaded zip).
param([string]$Build = (Join-Path $PSScriptRoot '..\build'), [string]$BaseUrl = 'https://example.com/releases')

$ErrorActionPreference = 'Stop'
$stage = Join-Path (Resolve-Path $Build) 'stage'
$ver = (Get-Content (Join-Path $stage 'version.txt') -TotalCount 1).Trim()
$dist = Join-Path $PSScriptRoot '..\dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$dist = (Resolve-Path $dist).Path

# Zip root must contain RareStudioApp.exe directly (the updater extracts into updates\<ver>\).
$tmp = Join-Path $env:TEMP "rarestudio-pkg-$ver"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
robocopy $stage $tmp /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

$zip = Join-Path $dist "RareStudio-$ver.zip"
if (Test-Path $zip) { Remove-Item $zip }
tar.exe -a -cf $zip -C $tmp .
Remove-Item $tmp -Recurse -Force

$sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
@{ version = $ver; url = "$BaseUrl/RareStudio-$ver.zip"; sha256 = $sha } |
    ConvertTo-Json | Set-Content (Join-Path $dist 'latest.json') -Encoding utf8
Write-Host "Wrote $zip ($sha)"
