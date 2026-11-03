# Plan: Fix audio capture (microphone) on Knulli / TrimUI Brick

## Résumé

Le code source (`sdl-audio-engine.cpp` et `sdl-audio-engine.h`) a déjà été modifié par l'utilisateur avec toutes les corrections nécessaires. Les tests confirment que le code produit déjà des fichiers .wav valides.

**Découverte des tests :** L'erreur "Operation not permitted" est résolue par le code actuel. Cependant, pour une solution à long terme robuste sur Knulli, il est recommandé de configurer WirePlumber au niveau système pour autoriser correctement la source microphone par défaut.

## État actuel

### Modifications déjà appliquées (✅ terminées)

**`shell/sdl/sdl-audio-engine.cpp`** (410 lignes) :
- `openCapture()` : interrogue SDL pour la liste des périphériques, essaie plusieurs candidats ALSA (default, plughw:0,0, plug:default), tente plusieurs formats (S16SYS à 44100/48000 Hz, 1/2 ch), utilise `SDL_AUDIO_ALLOW_ANY_CHANGE`
- `captureCallback()` : décode plusieurs formats audio (F32SYS, S16SYS, S32SYS, U16SYS, U8, S8)
- `listInputDevices()` : noms conviviaux ("Micro TrimUI (Interne)", "Micro externe")

**`shell/sdl/sdl-audio-engine.h`** (72 lignes) :
- Ajout du membre `SDL_AudioFormat captureFormat_ = AUDIO_S16SYS;`

## Solution à long terme recommandée : Configurer WirePlumber sur Knulli

### Raisonnement

Le code actuel avec SDL et les candidats ALSA fonctionne et produit des .wav valides. Cependant, laisser la couche système (WirePlumber) gérer le flux microphone évite les effets de bord sur la lecture générale (hw:0,0) et garantit que lecture et enregistrement cohabitent sans casser le système audio de Knulli.

### Tâches à effectuer sur l'appareil TrimUI Brick (Knulli)

1. **Identifier les règles WirePlumber existantes** sur le système Knulli
2. **Ajouter une règle de configuration** pour autoriser/activer correctement la source microphone par défaut
3. **Vérifier** que la lecture et l'enregistrement fonctionnent simultanément sans conflit

### Validation attendue

- `arecord` fonctionne depuis la ligne de commande
- MoonTRCK enregistre correctement depuis son interface
- La lecture audio continue de fonctionner normalement pendant l'enregistrement

## Validation du code (déjà terminée)

- [x] Code modifié et testé par l'utilisateur
- [x] Fichiers .wav produits sont valides
- [x] Plus d'erreur "Operation not permitted"

## Rollback

Si nécessaire, révertir `sdl-audio-engine.cpp` et `sdl-audio-engine.h` aux versions précédentes.

## Open Question

La configuration WirePlumber sur Knulli nécessite-t-elle un accès root/sudo sur l'appareil, ou peut-elle être faite via les outils de configuration de PortMaster ?