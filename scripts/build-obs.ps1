# Builds the slimmed-down libobs that RareStudio links against (engine + capture + encoders,
# no OBS UI, browser, scripting or plugins we don't use).
#
# Needs: Visual Studio 2022 Build Tools (C++ workload, Windows 11 SDK, ATL), Git, CMake.
param(
    [string]$ObsDir = 'C:\dev\obs-studio',
    [string]$Tag = '32.2.2'
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $ObsDir)) {
    git clone --depth 1 --branch $Tag --recurse-submodules --shallow-submodules https://github.com/obsproject/obs-studio.git $ObsDir
}

# Local patch: log shader parse errors instead of silently dropping them.
git -C $ObsDir apply --check (Join-Path $PSScriptRoot '..\patches\obs-log-effect-errors.patch') 2>$null
if ($LASTEXITCODE -eq 0) { git -C $ObsDir apply (Join-Path $PSScriptRoot '..\patches\obs-log-effect-errors.patch') }

$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmds = @(
    "call `"$vcvars`" >nul",
    "cd /d `"$ObsDir`"",
    'cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 -DENABLE_FRONTEND=OFF -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF ' +
        '-DENABLE_SCRIPTING=OFF -DENABLE_WEBSOCKET=OFF -DENABLE_VST=OFF -DENABLE_AJA=OFF -DENABLE_WEBRTC=OFF ' +
        '-DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_DECKLINK=OFF -DENABLE_CCACHE=OFF',
    'cmake --build build_x64 --config Release --parallel'
)
$bat = Join-Path $env:TEMP 'build-obs.bat'
$cmds | Set-Content $bat -Encoding ascii
cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw 'libobs build failed' }
