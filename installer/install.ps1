# Installs RareStudio into Program Files and creates the desktop + Start menu shortcuts.
#   RareStudio.exe   launcher (what the shortcuts point at; never changes between updates)
#   app\             the version that shipped with this install
# Updates later land in %LOCALAPPDATA%\RareStudio\updates and are picked up by the launcher.
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\build'),
    [string]$DesktopDir = [Environment]::GetFolderPath('Desktop')
)

$ErrorActionPreference = 'Stop'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
                 '-Build', "`"$(Resolve-Path $Build)`"", '-DesktopDir', "`"$DesktopDir`"")
    $p = Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
}

$Build = (Resolve-Path $Build).Path
$stage = Join-Path $Build 'stage'
$launcher = Join-Path $Build 'launcher\RareStudio.exe'
foreach ($f in $stage, $launcher) { if (-not (Test-Path $f)) { throw "Missing $f. Build first." } }

$dest = Join-Path $env:ProgramFiles 'RareStudio'
Get-Process RareStudioApp, RareStudio -ErrorAction SilentlyContinue | Stop-Process -Force

New-Item -ItemType Directory -Force $dest | Out-Null
$appDir = Join-Path $dest 'app'
if (Test-Path $appDir) { Remove-Item $appDir -Recurse -Force }
Copy-Item $launcher (Join-Path $dest 'RareStudio.exe') -Force

# robocopy: fast, skips debug symbols. Exit codes < 8 mean success.
robocopy $stage $appDir /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

$exe = Join-Path $dest 'RareStudio.exe'
$shell = New-Object -ComObject WScript.Shell
$targets = @(
    (Join-Path $DesktopDir 'RareStudio.lnk'),
    (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'RareStudio.lnk')
)
foreach ($lnk in $targets) {
    $s = $shell.CreateShortcut($lnk)
    $s.TargetPath = $exe
    $s.WorkingDirectory = $dest
    $s.IconLocation = "$exe,0"
    $s.Description = 'RareStudio'
    $s.Save()
}
Write-Host "Installed to $dest"
