# Plan — MOONGRANULAR : éditeur dédié MOONSET (moonset.cpp/h) séparé du sample editor

## Objectif

Séparer clairement les deux éditeurs :
1. **sample_editor.cpp/h** = éditeur SAMPLER uniquement, inchangé (revert toutes mes modifs)
2. **moonset.cpp/h** = éditeur MOONGRANULAR dédié, compilé et câblé, avec tous les params GRAIN/ROAM/MOTION en 3 colonnes sous le waveform, avec scroll

La page instrument (TYPE="MOONGRANULAR", STRETCH, scroll instrument) garde ses changements.

## Décisions

### 1. Revert sample_editor (éditeur sampler, inchangé)
- `sample_editor.cpp` : supprimer le bloc GRANULAR, revert `cursor_context` (cases 20-26), revert `handle_input` (cases 20-26), `content_y` déjà à l'original
- `sample_editor.h` : supprimer `granularScrollRow` et `granularUi`
- `cursor_move.h` : `sample_editor_step_row` sans scroll (revert)

### 2. Activer moonset (éditeur MOONGRANULAR dédié)
- Ajouter `ui/modules/moonset.cpp` et `ui/dispatch/screens/moonset.cpp` à `native/CMakeLists.txt`
- `moonset.cpp` (modules) : `MoonsetEditorModule` avec `MoonsetEditorState` — ajouter tous les params GRAIN/ROAM/MOTION (28 params) en 3 colonnes sous le waveform, typo `FONT_SCALE - 1`, scroll `moonsetScrollRow`
- `moonset.cpp` (dispatch/screens) : `moonset_screen`, `open_moonset`, `moonset_context`, `moonset_edit` — utiliser `MoonsetEditorState`/`MoonsetEditorModule`
- Changer le dispatch route : quand l'instrument est de type GRANULAR, ouvrir `moonset_screen` au lieu de `sample_editor_screen`

### 3. Params GRAIN/ROAM/MOTION dans moonset.cpp
3 colonnes, typo plus petite, espacement réduit :
- **GRAIN** : STATE, POSITION, SIZE, DENSITY, SPRAY, PITCH, TIME, ATTACK, DECAY, PAN, JITTER
- **ROAM** : STATE, MODE, RANGE, WANDER, SPEED, SMOOTH, BPM, DRIFT, STEP_RATE, INERTIA, TARGET
- **MOTION** : STATE, JUMP, HOLD, REV, ELASTIC, CHAOS, PROBABILITY, LFO_DEPTH, STEP_GLIDE, TRIGGER_RATE, SPIN_FACTOR

### 4. Scroll page moonset
`moonsetScrollRow` dans `MoonsetEditorState`, draw loop commence à ce scroll, auto-scroll quand le curseur sort de la fenêtre.

### 5. Fichiers à modifier / créer

#### Créer / activer : `native/ui/modules/moonset.cpp` + `native/ui/dispatch/screens/moonset.cpp`
- Déjà existants, à compléter avec les params granulaires + scroll
- Ajouter à CMakeLists.txt

#### Modifier : `native/CMakeLists.txt`
- Ajouter `ui/modules/moonset.cpp` et `ui/dispatch/screens/moonset.cpp`

#### Modifier : `native/ui/dispatch/route.cpp`
- Ligne 42 : ajouter une route pour MOONSET screen, ou conditionner sur le type d'instrument

#### Revert : `native/ui/modules/sample_editor.cpp`, `native/ui/modules/sample_editor.h`, `native/ui/cursor_move.h`
- Tout revenir à l'état avant mes modifications

## Validation
- Recompiler MoonTRCK : `powershell -ExecutionPolicy Bypass -File "shell\build-moontrck.ps1"`
- Sur la console : sampler = éditeur sample_editor (inchangé), MOONGRANULAR = éditeur moonset avec params granulaires
