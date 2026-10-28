# Builds an update package for the in-app updater.
#   1. Bump VERSION in the root CMakeLists.txt (project(... VERSION x.y.z)) and rebuild.
#   2. Run this script.
#   3. Upload dist\RareStudio-<ver>.zip and dist\latest.json to wherever "update/url" points
#      (e.g. a GitHub release, with latest.json's "url" pointing at the uploaded zip).
param([string]$Build = (Join-Path $PSScriptRoot '..\build'), [string]$BaseUrl = 'https://example.com/releases')

$ErrorActionPreference = 'Stop'
$stage = Join-Path (Resolve-Path $Build) 'stage'
$ver = (Get-Content (Join-Path $stage 'version.txt') -TotalCount 1).Trim()
# Signed as Rare6Media (self-signed; see scripts/sign.ps1) before it is packaged or deployed.
& (Join-Path $PSScriptRoot '../scripts/sign.ps1') -Build $Build
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
# Release notes come from this version's section of CHANGELOG.md (shown in the app and on the GitHub release).
$notes = ''
$changelog = Join-Path $PSScriptRoot '..\CHANGELOG.md'
if (Test-Path $changelog) {
    $take = $false; $lines = @()
    foreach ($l in Get-Content $changelog) {
        if ($l -match '^## ') { $take = ($l -match ('^## ' + [regex]::Escape($ver) + '($| )')); continue }
        if ($take) { $lines += $l }
    }
    $notes = ($lines -join "`n").Trim()
}
Set-Content (Join-Path $dist 'notes.md') $notes -Encoding utf8
@{ version = $ver; url = "$BaseUrl/RareStudio-$ver.zip"; sha256 = $sha; notes = $notes } |
    ConvertTo-Json | ForEach-Object { [IO.File]::WriteAllText((Join-Path $dist 'latest.json'), $_, (New-Object Text.UTF8Encoding $false)) } # no BOM: Qt's JSON parser rejects one
Write-Host "Wrote $zip ($sha)"
