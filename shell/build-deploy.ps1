# Build + deploy MoonTRCK to the console in one shot.
# =============================================================================
#   ./shell/build-deploy.ps1             # console par défaut : 10.239.4.149
#   ./shell/build-deploy.ps1 10.239.4.200 # console avec une autre IP
#
# La commande fait tout d'un coup :
#   1. build aarch64 via Docker (build-moontrck.sh dans le conteneur Linux),
#   2. kill de l'instance MoonTRCK qui tourne sur la console (en SSH),
#   3. scp du nouveau binaire + remplacement (mv) + chmod +x.
param([string]$Ip = "10.239.4.149")

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

Write-Host ">>> [1/2] BUILD (Docker -> build/portmaster-moontrck/stage/MoonTRCK.aarch64)" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "build-moontrck.ps1")
if ($LASTEXITCODE -ne 0) { throw "Le build a echoue - pas de deploiement." }

Write-Host ">>> [2/2] DEPLOY vers $Ip (kill instance SSH + remplacement)" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "deploy-console.ps1") $Ip
if ($LASTEXITCODE -ne 0) { throw "Le deploiement a echoue." }

Write-Host "OK - tout est fait : nouveau binaire en place sur la console. Relance le port depuis le menu." -ForegroundColor Green