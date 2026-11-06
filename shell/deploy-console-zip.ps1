# Ship the whole MoonTRCK PortMaster package (moontrck.zip) to the handheld, unzip it there,
# then leave it ready to launch.
#
#   powershell -ExecutionPolicy Bypass -File .\shell\deploy-console-zip.ps1
#   powershell -ExecutionPolicy Bypass -File .\shell\deploy-console-zip.ps1 10.239.4.200
#
# The binary-only swap is deploy-console.ps1; this one also replaces MoonTRCK.sh, port.json,
# gameinfo.xml and the licences, exactly as PortMaster would install the zip.
param([string]$Ip = "10.239.4.149")

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$zip  = Join-Path $root "build\portmaster-moontrck\moontrck.zip"
$dir  = "/userdata/roms/ports/moontrck"

if (-not (Test-Path -LiteralPath $zip)) {
    throw "No package: $zip (run shell\build-moontrck.ps1 first)"
}

Write-Host "Uploading $zip to $Ip ..." -ForegroundColor Cyan
scp -O $zip "root@${Ip}:/tmp/moontrck.zip"
if ($LASTEXITCODE -ne 0) { throw "scp failed" }

# ⚠️ Do NOT use `pkill -f MoonTRCK`: this very command line contains ".../MoonTRCK.aarch64", so the
# pattern matches the remote shell itself and kills it before `unzip` runs (that bug made every
# earlier deploy a silent no-op). Kill only PIDs whose /proc/<pid>/exe is really the binary.
Write-Host "Stopping MoonTRCK and unzipping on the console ..." -ForegroundColor Cyan
$kill = 'for p in /proc/[0-9]*; do e=$(readlink $p/exe 2>/dev/null); case $e in */MoonTRCK.aarch64*) kill ${p#/proc/} 2>/dev/null;; esac; done; sleep 1; '
ssh "root@$Ip" ($kill + 'mkdir -p ' + $dir + '; cd ' + $dir + ' && unzip -o /tmp/moontrck.zip >/dev/null && chmod +x MoonTRCK.aarch64 MoonTRCK.sh && rm -f /tmp/moontrck.zip && echo UNZIPPED')
if ($LASTEXITCODE -ne 0) { throw "ssh failed" }

Write-Host "OK. Launch with:" -ForegroundColor Green
Write-Host "  ssh root@$Ip `"nohup $dir/MoonTRCK.aarch64 >/tmp/moontrck.log 2>&1 </dev/null & disown; echo LAUNCHED`""
