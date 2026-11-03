# Rebuild + lance PocketTracker (MinGW).
# Usage depuis la racine du repo :  powershell -ExecutionPolicy Bypass -File shell/rebuild-run.ps1
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path (Join-Path $PSScriptRoot "..")
$build  = Join-Path $root "shell\build-mingw"
$exe    = Join-Path $build "pockettracker-sdl.exe"

# Ferme une instance éventuellement ouverte (verrouille l'exe => Permission denied sur le relink)
Get-Process -Name "pockettracker-sdl" -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill(); $_.WaitForExit() }

Write-Host "Build..." -ForegroundColor Cyan
cmake --build $build -j | Out-String
if ($LASTEXITCODE -ne 0) { throw "Build échoué" }

Write-Host "Lancement..." -ForegroundColor Green
& $exe
