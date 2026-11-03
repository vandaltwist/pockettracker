# Watch PocketTracker source and rebuild + restart on change.
# Usage: .\shell\watch-build.ps1
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $root "shell\build"
$exe = Join-Path $buildDir "Release\pockettracker-sdl.exe"

# Ensure build dir exists and is configured
if (-not (Test-Path $buildDir)) {
    Write-Host "Configuring CMake..." -ForegroundColor Cyan
    cmake -S "$root\shell" -B $buildDir -DCMAKE_BUILD_TYPE=Release | Out-Null
}

function Restart-App {
    param($Process)
    if ($Process -and -not $Process.HasExited) {
        try { $Process.Kill() } catch {}
        $Process.WaitForExit()
    }
    if (Test-Path $exe) {
        Write-Host "Launching $exe" -ForegroundColor Green
        return Start-Process $exe -WorkingDirectory $buildDir -PassThru
    }
    Write-Host "Executable not found yet: $exe" -ForegroundColor Yellow
    return $null
}

Write-Host "Watching for changes in native/ and shell/ ..." -ForegroundColor Cyan
Write-Host "Press Ctrl+C to stop." -ForegroundColor Gray

$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = $root
$watcher.Filter = "*.*"
$watcher.IncludeSubdirectories = $true
$watcher.NotifyFilter = [System.IO.NotifyFilters]::LastWrite -bor [System.IO.NotifyFilters]::FileName -bor [System.IO.NotifyFilters]::DirectoryName

$relevant = @("\native\", "\shell\")
$debounceMs = 1500
$timer = $null
$app = $null
$pending = $false

$action = {
    $path = $Event.SourceEventArgs.FullPath
    if ($path -match "\\native\\|\\shell\\") {
        $script:pending = $true
        if ($timer) { $timer.Dispose() }
        $timer = New-Object System.Timers.Timer($debounceMs)
        $timer.AutoReset = $false
        $timer.AddElapsedEventListener({
            if ($script:pending) {
                $script:pending = $false
                Write-Host "`n[$(Get-Date -Format 'HH:mm:ss')] Change detected in $path - rebuilding..." -ForegroundColor Cyan
                try {
                    cmake --build $buildDir --config Release 2>&1 | Out-String
                    $LASTEXITCODE
                    if ($LASTEXITCODE -eq 0) {
                        Write-Host "Build OK." -ForegroundColor Green
                        $script:app = Restart-App -Process $script:app
                    } else {
                        Write-Host "Build failed." -ForegroundColor Red
                    }
                } catch {
                    Write-Host "Build error: $_" -ForegroundColor Red
                }
            }
        })
        $timer.Start()
    }
}

$sub = Register-ObjectEvent -InputObject $watcher -EventName Changed -Action $action
$watcher.EnableRaisingEvents = $true

# Initial build + launch
Write-Host "Initial build..." -ForegroundColor Cyan
cmake --build $buildDir --config Release 2>&1 | Out-String
if ($LASTEXITCODE -eq 0) {
    $app = Restart-App -Process $null
} else {
    Write-Host "Initial build failed." -ForegroundColor Red
}

try {
    while ($true) { Start-Sleep -Seconds 1 }
} catch {
    Write-Host "`nStopping watcher..." -ForegroundColor Yellow
    $watcher.EnableRaisingEvents = $false
    Unregister-Event -SourceIdentifier $sub.Name -ErrorAction SilentlyContinue
    if ($timer) { $timer.Dispose() }
    Restart-App -Process $app
    Write-Host "Done." -ForegroundColor Gray
}
