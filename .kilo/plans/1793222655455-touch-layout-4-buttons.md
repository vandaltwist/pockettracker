# Plan : Touch layout 4 boutons + suppression des boutons épaulette/select/start

## Objectif

Remplacer le système de boutons actuel par exactement 4 touches directionnelles sur le touch (en croix, comme une manette), plus 4 touches clavier physiques qui envoient les mêmes actions. Supprimer totalement `L_SHIFT`, `R_SHIFT`, `SELECT`, `START` de l'enum `Button`. Réécrire le mapper de combos et les écrans qui utilisaient ces boutons supprimés.

---

## 1. Mapping final (touch + clavier, deux entrées séparées)

**Touch à l'écran (4 boutons en croix) :**
```
          UP
       X = PLAY
          ▲
LEFT ◄────┼────► RIGHT
 Y = SHIFT   A = OPTION
          ▼
       DOWN
    B = EDIT
```

**Clavier (touches physiques, envoie les mêmes boutons console) :**

| Touche clavier | Bouton console | Action |
|---|---|---|
| J | X | PLAY |
| N | Y | SHIFT (changement de page) |
| M | A | OPTION |
| K | B | EDIT |
| WASD / flèches | DPAD_* | Navigation |

**Tableau unifié :**

| Position touch | Bouton console | Action | Touche clavier |
|---|---|---|---|
| UP | X | PLAY | J |
| LEFT | Y | SHIFT | N |
| RIGHT | A | OPTION | M |
| DOWN | B | EDIT | K |

---

## 2. Boutons supprimés de l'enum `Button` (`native/ui/buttons.h`)

Supprimés définitivement :
- `L_SHIFT`
- `R_SHIFT`
- `SELECT`
- `START`

Conservés (8 valeurs) :
- `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`
- `A`, `B`, `X`, `Y`

---

## 3. Fichiers à modifier (Phase 1 : Core)

| Fichier | Ce qu'il faut faire |
|---|---|
| `native/ui/buttons.h` | Réduire l'enum `Button` de 12 à 8 valeurs. Mettre à jour `button_name()`. Supprimer les références aux 4 boutons retirés. |
| `shell/sdl/sdl-input.cpp` | `KEY_DEFAULTS` : ne garder que {J→X, N→Y, M→A, K→B} + DPAD (WASD/flèches). Supprimer toutes les autres entrées (U→L_SHIFT, I→R_SHIFT, LSHIFT→SELECT, SPACE→START, etc.). |
| `native/ui/touch_layout.h` | `left_rects()` : 4 touches directionnelles uniquement (UP/LEFT/RIGHT/DOWN). Supprimer L_SHIFT et SELECT du LEFT box. |
| `native/ui/button_mapper.h` | Réécriture complète. Plus de combos L/R/SELECT/START. Seuls les boutons A, B, X, Y et DPAD avec leurs mods (A+B, A+DPAD, B+DPAD, etc.). Supprimer `aPressedAlone`, `bPressedAlone`, `rComboArmed`, `selectPressedAlone`, `mods.l`, `mods.r`. |

---

## 4. Écrans à réécrire (Phase 2 : page par page)

Chaque écran utilise `InputDispatcher` avec des `Gesture` (ex: `Gesture::L_A`, `Gesture::START`, `Gesture::SELECT_A`). Il faut remplacer ou supprimer tous les cas qui utilisent les 4 boutons supprimés.

| # | Fichier | Écran | Gestures utilisant les boutons supprimés | Action |
|---|---|---|---|---|
| 1 | `project.cpp` | SONG / SETTINGS | `L_A`, `L_B`, `L_START` | Remplacer par équivalent sur A/B/X/Y/DPAD ou supprimer |
| 2 | `instrument.cpp` | INSTRUMENT / POOL / MODS | `A_START` | Remplacer ou supprimer |
| 3 | `sequencer.cpp` | SONG / CHAIN / PHRASE / TABLE | `L_B_A`, `L_START`, `L_A`, `L_B`, `L_*`, `R_START` | Réécriture massive |
| 4 | `sample_editor.cpp` | Sample Editor | `START`, `R_*`, `L_R` | Remplacer ou supprimer |
| 5 | `mixer.cpp` | MIXER | `A` uniquement | Impact faible, aucun changement |
| 6 | `midi.cpp` | MIDI | `A`, `B` uniquement | Impact faible, aucun changement |
| 7 | `scale_groove.cpp` | SCALE / GROOVE | `A`, `B_*` uniquement | Impact faible, aucun changement |
| 8 | `file_browser.cpp` | File Browser | `SELECT`, `SELECT_A`, `SELECT_B`, `SELECT_R`, `L_A`, `L_B`, `L_R`, `R_*` | Réécriture massive |

---

## 5. Combos qui doivent rester fonctionnels sur les 4 touches

Tous ces combos sont des paires de touches adjacentes sur la croix tactile, donc réalisables :

- `[OPTION]+[SHIFT]` → enter selection mode = RIGHT + LEFT
- `[EDIT]+[OPTION]` → cut / copy into buffer = DOWN + RIGHT
- `[SHIFT]+[OPTION]` → enters selection mode = LEFT + RIGHT
- `[SHIFT]+[EDIT]` → selection mode = LEFT + DOWN
- `[PLAY]` → start/stop song / chain = UP
- `[OPTION]+[SHIFT, then EDIT]` → clone = RIGHT + LEFT + DOWN

---

## 7. Décisions finales (questions ouvertes résolues)

1. **RIGHT box :** réduit à A (OPTION) et B (EDIT) uniquement. R_SHIFT et START sont supprimés du touch (et de l'enum `Button`).
2. **Combos conservés sur les 4 touches :** OPTION+EDIT, SHIFT+OPTION, OPTION+SHIFT, SHIFT+EDIT, PLAY. Tous sont des paires de touches adjacentes sur la croix tactile.

## 8. Validation

- [ ] Aucune référence résiduelle à `L_SHIFT`, `R_SHIFT`, `SELECT`, `START` dans le code source
- [ ] Pas de touche clavier en doublon dans `KEY_DEFAULTS`
- [ ] Le touch LEFT box n'a que 4 boutons directionnels
- [ ] Le RIGHT box ne contient que A et B
- [ ] Compilation native sans erreur

## Statut

Plan implementation-ready.
