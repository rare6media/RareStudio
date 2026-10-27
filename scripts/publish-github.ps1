# One command to put RareStudio on GitHub and turn on auto-updates:
#
#   powershell -ExecutionPolicy Bypass -File scripts\publish-github.ps1
#
# It signs you in (a one-time code is shown; enter it in your browser), creates the public
# repo "RareStudio" under your account, pushes the source, builds the update package for the
# current version, publishes it as a GitHub Release, and points the installed app at that feed.
# Safe to run again for every new version (bump VERSION in CMakeLists.txt and rebuild first).
param([string]$Repo = 'RareStudio')
$ErrorActionPreference = 'Continue' # native tools log progress to stderr; failures are caught by Assert-Ok
function Assert-Ok($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" } }
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) { winget install --id GitHub.cli -e --silent --accept-package-agreements --accept-source-agreements; $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User'); $gh = (Get-Command gh).Source }

# Existence checks write to stderr on "not found"; under 'Stop' PS 5.1 would treat that as fatal.
function Test-Gh { $p = $ErrorActionPreference; $ErrorActionPreference = 'Continue'; & $gh @args *> $null; $ok = ($LASTEXITCODE -eq 0); $ErrorActionPreference = $p; $ok }

if (-not (Test-Gh auth status)) { & $gh auth login --hostname github.com --git-protocol https --web --scopes 'repo,workflow' }

$login = (& $gh api user --jq .login).Trim()
$id = (& $gh api user --jq .id).Trim()
Write-Host "Signed in as $login"

# Commit with GitHub's private noreply address so your real email is not published.
if (-not (Test-Path .git)) { git init -b main | Out-Null }
git config user.name $login
git config user.email "$id+$login@users.noreply.github.com"
git add -A
git diff --cached --quiet; if ($LASTEXITCODE -ne 0) { git commit -m "RareStudio" | Out-Null }

if (-not (Test-Gh repo view "$login/$Repo")) {
    & $gh repo create "$login/$Repo" --public --source . --remote origin --push `
        --description 'A lighter OBS-style streaming studio: multi-stream, Shorts and Twitch clips, shader transitions.'
    Assert-Ok 'gh repo create'
} else {
    if (-not (git remote)) { git remote add origin "https://github.com/$login/$Repo.git" }
    git push -u origin HEAD
    Assert-Ok 'git push'
}

$version = ((Select-String -Path CMakeLists.txt -Pattern 'project\(RareStudio VERSION ([0-9.]+)').Matches[0].Groups[1].Value)
$base = "https://github.com/$login/$Repo/releases/download/v$version"
& (Join-Path $PSScriptRoot '..\installer\package-update.ps1') -BaseUrl $base

if (-not (Test-Gh release view "v$version" --repo "$login/$Repo")) {
    & $gh release create "v$version" "dist\RareStudio-$version.zip" "dist\latest.json" --repo "$login/$Repo" `
        --title "RareStudio $version" --notes "RareStudio $version. Unzip, or let an installed copy update itself."
    Assert-Ok 'gh release create'
} else {
    & $gh release upload "v$version" "dist\RareStudio-$version.zip" "dist\latest.json" --clobber --repo "$login/$Repo"
    Assert-Ok 'gh release upload'
}

# Point the installed app at this feed (same setting as File > Settings > Updates).
$feed = "https://github.com/$login/$Repo/releases/latest/download/latest.json"
reg add 'HKCU\Software\RareStudio\RareStudio\update' /v url /d $feed /f | Out-Null
Write-Host "Done. Repo: https://github.com/$login/$Repo   Feed: $feed"
