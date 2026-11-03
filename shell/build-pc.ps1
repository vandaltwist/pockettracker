# Kill the running PC build, rebuild it, then launch it.
#
#   powershell -ExecutionPolicy Bypass -File .\shell\build-pc.ps1
#   powershell -ExecutionPolicy Bypass -File .\shell\build-pc.ps1 -NoRun
#
# The build directory is the one the project already uses: shell\build-pc (Visual Studio generator).
param(
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$root  = Resolve-Path "$PSScriptRoot/.."
$build = Join-Path $root "shell\build-pc"
$exe   = Join-Path $build "Debug\pockettracker-sdl.exe"

# A running instance locks the .exe and the link fails.
Get-Process -Name "pockettracker-sdl" -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Kill(); $_.WaitForExit() }

Write-Host "Building MoonTRCK (PC) ..." -ForegroundColor Cyan
cmake --build $build -j
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }

if (-not $NoRun) {
    Write-Host "Launching $exe" -ForegroundColor Green
    Start-Process -FilePath $exe
} else {
    Write-Host "OK: $exe" -ForegroundColor Green
}
