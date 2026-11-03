# Ship the built aarch64 binary to the handheld.
#
# The running MoonTRCK is killed before the swap: a live binary makes the copy fail with
# "text file busy" (same thing as the Windows lock that blocks the link). The upload first lands
# under a temp name, then one ssh session kills the game, swaps the file in and marks it runnable.
#
#   .\deploy-console.ps1                # root@10.239.4.149
#   .\deploy-console.ps1 10.239.4.200   # another address
param([string]$Ip = "10.239.4.149")

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$bin  = Join-Path $root "build\portmaster-moontrck\stage\MoonTRCK.aarch64"
$dir  = "/userdata/roms/ports/moontrck"

if (-not (Test-Path $bin)) { throw "Pas de binaire : $bin (lance build-moontrck.sh d'abord)" }

Write-Host "Copie vers $Ip ..." -ForegroundColor Cyan
scp $bin "root@${Ip}:$dir/MoonTRCK.new"
if ($LASTEXITCODE -ne 0) { throw "scp a echoue" }

# ⚠️ '[M]oonTRCK' : le motif entre crochets ne se matche pas lui-meme, sinon pkill tue aussi le
# shell distant et le mv/chmod qui suit ne tourne jamais.
ssh "root@$Ip" "pkill -f '[M]oonTRCK' || true; sleep 1; mv -f $dir/MoonTRCK.new $dir/MoonTRCK.aarch64 && chmod +x $dir/MoonTRCK.aarch64 && echo REMPLACE"
if ($LASTEXITCODE -ne 0) { throw "ssh a echoue" }

Write-Host "OK - relance MoonTRCK depuis la console." -ForegroundColor Green
