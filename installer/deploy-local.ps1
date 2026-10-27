# Installs the freshly built stage tree as an update for the launcher, then refreshes the desktop shortcut
# (and the launcher .exe itself, when it changed) so the icon and version you click are the new ones.
#
#   installer\deploy-local.ps1                 # deploy; fails if RareStudio is open
#   installer\deploy-local.ps1 -Close -Start   # close RareStudio politely, deploy, start the new version
#
# No admin needed for the update itself: the launcher (C:\Program Files\RareStudio\RareStudio.exe) runs the
# newest version found in %LOCALAPPDATA%\RareStudio\updates. Only replacing the launcher asks for UAC, and
# skipping that prompt is fine (the shortcut carries its own icon). Bump VERSION in CMakeLists.txt first:
# the launcher only picks an update that is newer than the installed version.
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\build'),
    [switch]$Close, # ask a running RareStudio to close (it saves everything, and asks first if you are live)
    [switch]$Start  # start the new version afterwards
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$stage = Join-Path (Resolve-Path $Build) 'stage'
$ver = (Get-Content (Join-Path $stage 'version.txt') -TotalCount 1).Trim()
$base = Join-Path $env:LOCALAPPDATA 'RareStudio'
$dest = Join-Path $base "updates\$ver"
$installDir = Join-Path $env:ProgramFiles 'RareStudio'
$installedLauncher = Join-Path $installDir 'RareStudio.exe'

# ---- Close the running app -----------------------------------------------------------------------
$running = Get-Process RareStudioApp -ErrorAction SilentlyContinue
if ($running) {
    if (-not $Close) { throw 'Close RareStudio first (or pass -Close).' }
    Write-Host 'Closing RareStudio...'
    # A normal window close, never a kill: the app autosaves on the way out and asks before it stops a stream.
    $running | ForEach-Object { [void]$_.CloseMainWindow() }
    $running | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue
    if (Get-Process RareStudioApp -ErrorAction SilentlyContinue) {
        throw 'RareStudio is still open (it may be asking whether to stop streaming). Close it and run this again.'
    }
}

# ---- The update itself ---------------------------------------------------------------------------
$installedVer = $null
$installedVerFile = Join-Path $installDir 'app\version.txt'
if (Test-Path $installedVerFile) { $installedVer = (Get-Content $installedVerFile -TotalCount 1).Trim() }
if ($installedVer -and [version]$ver -le [version]$installedVer) {
    Write-Warning "Built version $ver is not newer than the installed $installedVer, so the launcher will keep starting $installedVer. Bump VERSION in CMakeLists.txt."
}
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
robocopy $stage $dest /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
New-Item -ItemType File (Join-Path $dest 'ok') | Out-Null   # written last: marks it complete
Write-Host "RareStudio $ver deployed to $dest"

# ---- Launcher .exe (its embedded icon is what Explorer shows for it) ------------------------------
$builtLauncher = Join-Path (Resolve-Path $Build) 'launcher\RareStudio.exe'
if ((Test-Path $installedLauncher) -and (Test-Path $builtLauncher) -and
    (Get-FileHash $builtLauncher).Hash -ne (Get-FileHash $installedLauncher).Hash) {
    try {
        Copy-Item $builtLauncher $installedLauncher -Force
        Write-Host 'Launcher updated.'
    } catch {
        Write-Host 'Updating the launcher in Program Files needs admin rights; asking Windows (UAC)...'
        $cmd = "Copy-Item -LiteralPath '$builtLauncher' -Destination '$installedLauncher' -Force"
        try {
            $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile', '-Command', $cmd
            if ($p.ExitCode -eq 0) { Write-Host 'Launcher updated.' } else { Write-Warning 'Launcher not updated.' }
        } catch {
            Write-Warning 'Launcher not updated (UAC declined). The update still works; only that .exe keeps its old icon.'
        }
    }
}

# ---- Desktop shortcut ------------------------------------------------------------------------------
# The icon lives in a per-version file: Windows caches shortcut icons by path, so a new path is what
# makes the desktop show a new logo straight away.
$iconDir = Join-Path $base 'icons'
New-Item -ItemType Directory -Force $iconDir | Out-Null
$icon = Join-Path $iconDir "rarestudio-$ver.ico"
Copy-Item (Join-Path $root 'assets\rarestudio.ico') $icon -Force

$target = if (Test-Path $installedLauncher) { $installedLauncher } else { Join-Path $dest 'RareStudioApp.exe' }
$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'RareStudio.lnk'
$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($lnk)
$s.TargetPath = $target
$s.WorkingDirectory = Split-Path $target
$s.IconLocation = "$icon,0"
$s.Description = "RareStudio $ver"
$s.Save()
Write-Host "Desktop shortcut refreshed: $lnk"

# Older icon files are no longer referenced by anything.
Get-ChildItem $iconDir -Filter 'rarestudio-*.ico' | Where-Object { $_.FullName -ne $icon } | Remove-Item -Force

if ($Start) {
    Start-Process $target
    Write-Host "Started RareStudio $ver."
} else {
    Write-Host 'Start it from your desktop shortcut.'
}
