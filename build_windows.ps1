param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir

function Fail([string]$Message) {
    Write-Host ""
    Write-Host "BUILD FAILED: $Message" -ForegroundColor Red
    exit 1
}

Write-Host "Gesture Rack - Windows Release Build" -ForegroundColor Cyan
Write-Host "Project: $ProjectDir"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Fail "CMake was not found. Install CMake 3.22+ and Visual Studio with 'Desktop development with C++'."
}

$versionText = (& cmake --version | Select-Object -First 1)
Write-Host $versionText

# CMake's default Windows generator is the newest installed Visual Studio.
# Verify that a native MSVC toolchain is actually present so errors are clear.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        Fail "Visual Studio is installed, but the MSVC C++ toolchain is missing. Add the 'Desktop development with C++' workload."
    }
    Write-Host "Visual Studio: $vsPath"
} else {
    Write-Host "vswhere.exe not found; CMake will still try its default Windows generator." -ForegroundColor Yellow
}

$BuildDir = Join-Path $ProjectDir 'build'
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning previous build directory..."
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host ""
Write-Host "[1/2] Configuring..." -ForegroundColor Cyan
& cmake -S $ProjectDir -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T host=x64 -DGESTURERACK_FETCH_JUCE=ON
if ($LASTEXITCODE -ne 0) {
    Fail "CMake configure failed. If the build directory came from another generator, run BuildWindows-Clean.bat."
}

Write-Host ""
Write-Host "[2/2] Building VST3 + Standalone (Release, low-memory serial mode)..." -ForegroundColor Cyan
$env:CMAKE_BUILD_PARALLEL_LEVEL = "1"
& cmake --build $BuildDir --config Release --target GestureRack_VST3 GestureRack_Standalone --parallel 1
if ($LASTEXITCODE -ne 0) {
    Fail "Compilation failed. Copy the first compiler error (not the final summary) back into ChatGPT."
}

$vst3 = Get-ChildItem -Path $BuildDir -Recurse -Directory -Filter '*.vst3' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '[\\/]Release[\\/]' } |
    Select-Object -First 1

$standalone = Get-ChildItem -Path $BuildDir -Recurse -File -Filter 'Gesture Rack.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '[\\/]Release[\\/]' } |
    Select-Object -First 1

Write-Host ""
Write-Host "BUILD SUCCESS" -ForegroundColor Green
if ($vst3) {
    Write-Host "VST3:       $($vst3.FullName)" -ForegroundColor Green
} else {
    Write-Host "VST3 target built, but the bundle path was not auto-detected." -ForegroundColor Yellow
}

if ($standalone) {
    Write-Host "Standalone: $($standalone.FullName)" -ForegroundColor Green
}

Write-Host ""
Write-Host "Expected VST3 folder:"
Write-Host "  $BuildDir\GestureRack_artefacts\Release\VST3\Gesture Rack.vst3"
