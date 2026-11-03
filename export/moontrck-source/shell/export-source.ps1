# Export every MoonTRCK source file into one clean folder (and a zip) for external editing.
#
#   powershell -ExecutionPolicy Bypass -File .\shell\export-source.ps1
#   powershell -ExecutionPolicy Bypass -File .\shell\export-source.ps1 -OutDir D:\MoonTRCK-src
#   powershell -ExecutionPolicy Bypass -File .\shell\export-source.ps1 -NoZip
#
# Build trees, .git, plans and logs are left out: what is exported is the source, not a build.
param(
    [string]$OutDir = "",
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/.."

if (-not $OutDir) { $OutDir = Join-Path $root "export\moontrck-source" }
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

# ⚠️ The export must never be inside the tree it is copying.
if ($OutDir.TrimEnd('\') -eq $root.Path.TrimEnd('\')) {
    throw "OutDir cannot be the repository root."
}

Write-Host "Exporting MoonTRCK sources to $OutDir ..." -ForegroundColor Cyan

if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir | Out-Null

# Directories that are generated, caches, or local notes.
$excludeDirs = @(
    (Join-Path $root ".git"),
    (Join-Path $root ".kilo"),
    (Join-Path $root "build"),
    (Join-Path $root "export"),
    (Join-Path $root "plans"),
    (Join-Path $root "shell\build-pc"),
    (Join-Path $root "shell\build-mingw"),
    (Join-Path $root "shell\build-linux"),
    (Join-Path $root "shell\build-miyoo"),
    (Join-Path $root "shell\build-portmaster"),
    (Join-Path $root "app\build"),
    (Join-Path $root ".gradle")
)

$excludeFiles = @(
    "logs-brick.txt",
    "firebase-debug.log",
    "RAPPORT_RECORD_TRIMUI.md"
)

# robocopy exit codes 0..7 are success; 8+ is a real failure.
robocopy $root $OutDir /E /NFL /NDL /NJH /NJS /XD @excludeDirs /XF @excludeFiles | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed (exit $LASTEXITCODE)" }

# A short manifest so the external editor knows what this tree is.
$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
@"
MoonTRCK source export
Generated: $stamp
From:      $($root.Path)

Build the PC version:
    cmake --build ".\shell\build-pc" -j
    .\shell\build-pc\Debug\pockettracker-sdl.exe

Build the handheld version (Docker, aarch64):
    powershell -ExecutionPolicy Bypass -File .\shell\build-moontrck.ps1
    artifact: .\build\portmaster-moontrck\moontrck.zip

The granular editor is MOONSET:
    native/ui/modules/moonset.h / .cpp
    native/ui/dispatch/screens/moonset.cpp
    native/granular/granular-voice.h
"@ | Set-Content -LiteralPath (Join-Path $OutDir "EXPORT-README.txt") -Encoding UTF8

$fileCount = (Get-ChildItem -LiteralPath $OutDir -Recurse -File | Measure-Object).Count
Write-Host "Copied $fileCount files." -ForegroundColor Green

if (-not $NoZip) {
    $zip = "$OutDir.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Write-Host "Zipping to $zip ..." -ForegroundColor Cyan
    Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zip -Force
    Write-Host "OK: $zip" -ForegroundColor Green
}
