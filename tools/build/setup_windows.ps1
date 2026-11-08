# BlockPlanet: set up and build on Windows from a fresh checkout and your own ROM.
#
# Needs: Visual Studio 2022 (or its Build Tools) with "Desktop development with
# C++", Git for Windows, and Python 3.11+ on PATH. Run from the repository root
# in PowerShell:
#
#   powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -Rom C:\path\to\Tetrisphere.z64
#
# Everything made from the ROM (the recompiled game code, the audio microcode)
# stays in .local\ and is never committed. Re-running is safe: steps already
# done are quick. See docs/qa/pc-setup-windows.md.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [switch]$SkipDependencies
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root

function Step([string]$text) { Write-Host "`n== $text" -ForegroundColor Cyan }
function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE): $($arguments -join ' ')" }
}

Step 'Checking tools'
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is not on PATH. Install Git for Windows and open a new PowerShell.' }
# Find a real Python 3.11+. Windows ships a "python" shortcut that only opens
# the Microsoft Store, and python.org's installer leaves PATH alone unless asked,
# so ask the "py" launcher first, then "python".
function Find-Python {
    $tries = @()
    if (Get-Command py -ErrorAction SilentlyContinue) { $tries += ,@('py', '-3') }
    if (Get-Command python -ErrorAction SilentlyContinue) { $tries += ,@('python') }
    foreach ($t in $tries) {
        $exe = $t[0]; $pre = @($t | Select-Object -Skip 1)
        try {
            $out = (& $exe @pre -c "import sys; print('%d.%d|%s' % (sys.version_info[0], sys.version_info[1], sys.executable))" 2>$null | Out-String).Trim()
        } catch { continue }
        if ($out -match '^3\.(\d+)\|(.+)$' -and [int]$Matches[1] -ge 11) { return $Matches[2] }
    }
    return $null
}
$python = Find-Python
if (-not $python) {
    throw 'Python 3.11 or newer was not found (the "python" command may be Windows'' Store shortcut). Install it from https://www.python.org/downloads/, then open a new PowerShell.'
}
Write-Host "python: $python"
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 (or its Build Tools) is not installed.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio is missing the "Desktop development with C++" workload.' }
# The Visual Studio developer environment (cl, cmake, ninja) for this session.
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
cmd /c "`"$devcmd`" -arch=amd64 -host_arch=amd64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    throw 'The C++ compiler (cl) was not found. In the Visual Studio Installer, choose Modify and tick "Desktop development with C++".'
}
# CMake and Ninja are optional parts of Visual Studio's C++ workload. Use
# Visual Studio's copies when they are there, else install them for this user
# from Python's package index.
$vsCMake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
foreach ($dir in @((Join-Path $vsCMake 'CMake\bin'), (Join-Path $vsCMake 'Ninja'))) {
    if (Test-Path $dir) { $env:Path = "$dir;$env:Path" }
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -or -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Host 'CMake or Ninja is missing from Visual Studio; installing them with pip (for this user only).'
    Run $python @('-m', 'pip', 'install', '--user', '--upgrade', 'cmake', 'ninja')
    $userScripts = (& $python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))").Trim()
    $env:Path = "$userScripts;$env:Path"
}
foreach ($tool in @('cmake', 'ninja')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool could not be found or installed." }
}
Write-Host "cmake: $((Get-Command cmake).Source)"
Write-Host "ninja: $((Get-Command ninja).Source)"
$cmake = (Get-Command cmake).Source

Step 'Your ROM (kept private in .local\m0)'
New-Item -ItemType Directory -Force -Path '.local\m0' | Out-Null
$romCopy = Join-Path $root '.local\m0\tetrisphere-us.z64'
Copy-Item -LiteralPath $Rom -Destination $romCopy -Force

if (-not $SkipDependencies) {
    Step 'Fetching the pinned dependencies (N64Recomp, N64ModernRuntime, RT64)'
    Run $python @('tools\build\fetch_dependencies.py', '--root', '.local\deps')
}

Step 'Building the recompilers (N64Recomp, RSPRecomp)'
$toolBuild = '.local\m1\toolchain\n64recomp-build'
Run $cmake @('-S', '.local\deps\N64Recomp', '-B', $toolBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
Run $cmake @('--build', $toolBuild, '--target', 'N64Recomp', 'RSPRecomp')

Step 'Generating the game code from your ROM'
if (Test-Path '.local\m1\linux-generated') { Remove-Item -Recurse -Force '.local\m1\linux-generated' }
if (Test-Path '.local\m1\linux-generated-manifest.json') { Remove-Item -Force '.local\m1\linux-generated-manifest.json' }
Run $python @('-m', 'tools.recomp.generate', 'create-receipt')
Run $python @('-m', 'tools.recomp.generate', 'generate', '--rom', $romCopy,
    '--output', '.local\m1\linux-generated', '--manifest', '.local\m1\linux-generated-manifest.json',
    '--recompiler', (Join-Path $toolBuild 'N64Recomp.exe'))

Step 'Generating the audio microcode from your ROM'
Run $python @('tools\build\extract_audio_ucode.py', '--rom', $romCopy)
Run (Join-Path $toolBuild 'RSPRecomp.exe') @('config\recomp\audio-rsp.toml')

Step 'Building BlockPlanet'
& (Join-Path $PSScriptRoot 'build_windows.ps1') `
    -SourceRoot $root -BuildRoot (Join-Path $root '.local\m1\build-windows') `
    -CMakeExe $cmake -PythonExe $python -Rom $romCopy `
    -GeneratedDir (Join-Path $root '.local\m1\linux-generated\functions') `
    -GenerationManifest (Join-Path $root '.local\m1\linux-generated-manifest.json') `
    -RspRecompSource (Join-Path $root '.local\m1\microcode\audio-rsp-recompiled.c')

$exe = Join-Path $root '.local\m1\build-windows\Release\tetrisphere-m1.exe'
Step "Done: $exe"
Write-Host 'Run it from that folder. Your settings, saves and logs go to C:\ProgramData\BlockPlanet.'
