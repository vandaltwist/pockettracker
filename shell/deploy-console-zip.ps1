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
scp $zip "root@${Ip}:/tmp/moontrck.zip"
if ($LASTEXITCODE -ne 0) { throw "scp failed" }

# ⚠️ '[M]oonTRCK' keeps pkill from matching this very shell command.
Write-Host "Stopping MoonTRCK and unzipping on the console ..." -ForegroundColor Cyan
ssh "root@$Ip" "pkill -f '[M]oonTRCK' || true; sleep 1; mkdir -p $dir; cd $dir && unzip -o /tmp/moontrck.zip >/dev/null && chmod +x MoonTRCK.aarch64 MoonTRCK.sh && rm -f /tmp/moontrck.zip && echo UNZIPPED"
if ($LASTEXITCODE -ne 0) { throw "ssh failed" }

Write-Host "OK. Launch with:" -ForegroundColor Green
Write-Host "  ssh root@$Ip `"nohup $dir/MoonTRCK.aarch64 >/tmp/moontrck.log 2>&1 </dev/null & disown; echo LAUNCHED`""
