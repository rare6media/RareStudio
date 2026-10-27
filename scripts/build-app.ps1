# Builds RareStudio itself into .\build (the runnable tree ends up in .\build\stage).
#
# Needs: scripts\build-obs.ps1 already run, Qt 6.8 (MSVC 2022 64-bit, with WebEngine) and Ninja.
#   python -m pip install aqtinstall
#   python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qtwebengine qtwebchannel qtpositioning qtmultimedia
param(
    [string]$Qt = 'C:/Qt/6.8.3/msvc2022_64',
    [string]$Build = (Join-Path $PSScriptRoot '..\build')
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$bat = Join-Path $env:TEMP 'build-rarestudio.bat'
@(
    "call `"$vcvars`" >nul",
    "cd /d `"$root`"",
    "cmake -S . -B `"$Build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$Qt",
    "cmake --build `"$Build`""
) | Set-Content $bat -Encoding ascii
cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Write-Host "Done. Run: $Build\stage\RareStudioApp.exe   (install with installer\install.ps1)"
