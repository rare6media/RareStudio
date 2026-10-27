# One command to put RareStudio on GitHub and turn on auto-updates:
#
#   powershell -ExecutionPolicy Bypass -File scripts\publish-github.ps1
#
# It signs you in (a one-time code is shown; enter it in your browser), creates the public
# repo "RareStudio" under your account, pushes the source, builds the update package for the
# current version, publishes it as a GitHub Release, and points the installed app at that feed.
# Safe to run again for every new version (bump VERSION in CMakeLists.txt and rebuild first).
param([string]$Repo = 'RareStudio')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) { winget install --id GitHub.cli -e --silent --accept-package-agreements --accept-source-agreements; $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User'); $gh = (Get-Command gh).Source }

& $gh auth status *> $null
if ($LASTEXITCODE -ne 0) { & $gh auth login --hostname github.com --git-protocol https --web --scopes 'repo,workflow' }

$login = (& $gh api user --jq .login).Trim()
$id = (& $gh api user --jq .id).Trim()
Write-Host "Signed in as $login"

# Commit with GitHub's private noreply address so your real email is not published.
if (-not (Test-Path .git)) { git init -b main | Out-Null }
git config user.name $login
git config user.email "$id+$login@users.noreply.github.com"
git add -A
git diff --cached --quiet; if ($LASTEXITCODE -ne 0) { git commit -m "RareStudio" | Out-Null }

& $gh repo view "$login/$Repo" *> $null
if ($LASTEXITCODE -ne 0) {
    & $gh repo create "$login/$Repo" --public --source . --remote origin --push `
        --description 'A lighter OBS-style streaming studio: multi-stream, Shorts and Twitch clips, shader transitions.'
} else {
    if (-not (git remote)) { git remote add origin "https://github.com/$login/$Repo.git" }
    git push -u origin HEAD
}

$version = ((Select-String -Path CMakeLists.txt -Pattern 'project\(RareStudio VERSION ([0-9.]+)').Matches[0].Groups[1].Value)
$base = "https://github.com/$login/$Repo/releases/download/v$version"
& (Join-Path $PSScriptRoot '..\installer\package-update.ps1') -BaseUrl $base

& $gh release view "v$version" --repo "$login/$Repo" *> $null
if ($LASTEXITCODE -ne 0) {
    & $gh release create "v$version" "dist\RareStudio-$version.zip" "dist\latest.json" --repo "$login/$Repo" `
        --title "RareStudio $version" --notes "RareStudio $version. Unzip, or let an installed copy update itself."
} else {
    & $gh release upload "v$version" "dist\RareStudio-$version.zip" "dist\latest.json" --clobber --repo "$login/$Repo"
}

# Point the installed app at this feed (same setting as File > Settings > Updates).
$feed = "https://github.com/$login/$Repo/releases/latest/download/latest.json"
reg add 'HKCU\Software\RareStudio\RareStudio\update' /v url /d $feed /f | Out-Null
Write-Host "Done. Repo: https://github.com/$login/$Repo   Feed: $feed"
