# Ship the built aarch64 binary to the handheld.
#
# The running MoonTRCK is stopped before the swap: a live binary makes the copy fail with
# "text file busy" (same thing as the Windows lock that blocks the link). The upload first lands
# under a temp name, then one ssh session stops the game, swaps the file in and marks it runnable.
#
# Two traps this script avoids, both of which used to make a deploy silently do nothing:
#
#  1. `pkill -f MoonTRCK` matches the REMOTE SHELL'S OWN command line, because that command line
#     contains ".../MoonTRCK.aarch64". It killed the session before `mv` ran, so the console kept
#     the old binary. We now kill only PIDs whose /proc/<pid>/exe really is the binary.
#
#  2. `cut -d" " -f1` loses its quotes on the way through PowerShell, so `cut` errors out. We use
#     `cut -c1-64` instead (a sha256 hex digest is exactly 64 characters).
#
# And it REFUSES to claim success unless the SHA256 read back from the console is identical to the
# local one, so a stale build can never be deployed silently again.
#
#   .\deploy-console.ps1                # root@10.239.4.149
#   .\deploy-console.ps1 10.239.4.200   # another address
param([string]$Ip = "10.239.4.149")

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$bin  = Join-Path $root "build\portmaster-moontrck\stage\MoonTRCK.aarch64"

if (-not (Test-Path -LiteralPath $bin)) { throw "Pas de binaire : $bin (lance build-moontrck.sh d'abord)" }

$localHash = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash.ToLower()
$info      = Get-Item -LiteralPath $bin

Write-Host ""
Write-Host "=== MoonTRCK : deploiement console ===" -ForegroundColor Cyan
Write-Host ("Source : " + $info.FullName)
Write-Host ("Taille : " + $info.Length + " octets")
Write-Host ("Date   : " + $info.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))
Write-Host ("SHA256 : " + $localHash)
Write-Host ("Cible  : root@" + $Ip)
Write-Host ""

Write-Host "Envoi du binaire ..." -ForegroundColor Cyan
scp -q $bin "root@${Ip}:/tmp/MoonTRCK.new"
if ($LASTEXITCODE -ne 0) { throw "scp a echoue (console allumee ? bon IP ? mauvais mot de passe ?)" }

# One remote session. Kept free of double quotes and of the string "MoonTRCK.aarch64" in any
# kill pattern, so nothing can match and kill this shell.
$remote = 'D=/roms/ports/moontrck; ' +
    '[ -d $D ] || D=/userdata/roms/ports/moontrck; ' +
    'if [ ! -d $D ]; then ' +
        'echo NO_DIR; ' +
        'echo ---ROMS---; ls -la /roms/ports 2>&1 | head -40; ' +
        'echo ---USERDATA---; ls -la /userdata/roms/ports 2>&1 | head -40; ' +
        'rm -f /tmp/MoonTRCK.new; exit 3; ' +
    'fi; ' +
    'echo DIR=$D; ' +
    'echo ID_ROM=$(stat -c %d:%i /roms/ports/moontrck 2>/dev/null); ' +
    'echo ID_USR=$(stat -c %d:%i /userdata/roms/ports/moontrck 2>/dev/null); ' +
    'echo BEFORE=$(sha256sum $D/MoonTRCK.aarch64 2>/dev/null | cut -c1-64); ' +
    'for p in /proc/[0-9]*; do e=$(readlink $p/exe 2>/dev/null); case $e in */MoonTRCK.aarch64*) kill ${p#/proc/} 2>/dev/null;; esac; done; ' +
    'sleep 1; ' +
    'mv -f /tmp/MoonTRCK.new $D/MoonTRCK.aarch64 || exit 4; ' +
    'chmod +x $D/MoonTRCK.aarch64; ' +
    'echo AFTER=$(sha256sum $D/MoonTRCK.aarch64 | cut -c1-64); ' +
    'echo SIZE=$(stat -c %s $D/MoonTRCK.aarch64); ' +
    'echo WHEN=$(stat -c %y $D/MoonTRCK.aarch64)'

Write-Host "Remplacement sur la console ..." -ForegroundColor Cyan
$out  = ssh "root@$Ip" $remote
$code = $LASTEXITCODE
$out | Where-Object { $_ -ne "" } | ForEach-Object { Write-Host ("  " + $_) }

if ($code -eq 3) {
    throw "MoonTRCK n'est installe ni dans /roms/ports/moontrck ni dans /userdata/roms/ports/moontrck. Rien n'a ete change (voir les listes ci-dessus)."
}
if ($code -eq 4) {
    throw "Le binaire n'a pas pu etre pose (mv refuse : /roms en lecture seule ?). Rien n'a ete change."
}
if ($code -ne 0) { throw "ssh a echoue (code $code)" }

$after = ($out | Where-Object { $_ -like "AFTER=*" }) -replace "^AFTER=", ""
if ($after -ne $localHash) {
    Write-Host ""
    Write-Host "ECHEC : le binaire sur la console ne correspond pas au binaire local." -ForegroundColor Red
    Write-Host ("  local   : " + $localHash)
    Write-Host ("  console : " + $after)
    throw "Deploiement NON verifie"
}

Write-Host ""
Write-Host "OK : hash identique, la console execute bien CE build." -ForegroundColor Green
Write-Host "Quitte MoonTRCK dans le menu puis relance-le." -ForegroundColor Green
