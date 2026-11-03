# Build MoonTRCK for PortMaster (aarch64) via Docker, from Windows PowerShell.
# Prerequisite: Docker Desktop running with Linux containers.
#
# .\shell\build-moontrck.ps1
#
# ⚠️ Les dossiers de build sont purgés ICI, côté Windows. Un `rm -rf` lancé dans le conteneur sur le
# bind mount NTFS échoue aléatoirement ("Directory not empty") et laisse un arbre à moitié supprimé :
# cmake/ninja repartent alors sur un état corrompu (config.h et *.o.d manquants). `rd /s /q` est fiable.

param(
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path "$PSScriptRoot/.."

function Remove-BuildDir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    for ($i = 1; $i -le 5; $i++) {
        cmd /c rd /s /q "`"$path`"" 2>$null
        if (-not (Test-Path -LiteralPath $path)) {
            Write-Host "cleaned $path"
            return
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Impossible de supprimer $path (verrouillé ?). Ferme l'explorateur/antivirus et relance."
}

if (-not $NoClean) {
    Remove-BuildDir "$repoRoot\build\aarch64-moontrck"
    Remove-BuildDir "$repoRoot\build\portmaster-moontrck"
}

docker build -t pockettracker-build -f "$repoRoot\shell\Dockerfile.portmaster" "$repoRoot\shell"

docker run --rm `
    -v "${repoRoot}:/src" `
    pockettracker-build `
    bash /src/shell/build-moontrck.sh

if ($LASTEXITCODE -ne 0) {
    throw "build failed (exit $LASTEXITCODE) - le binaire n'a PAS été mis à jour."
}

Write-Host "Done. Artifact: $repoRoot\build\portmaster-moontrck\moontrck.zip"
