# Plan — Chaîne : copier/coller une cellule via EDIT + mappage boutons EDIT/OPTION/PLAY/SHIFT dans SETTINGS

## But

Deux fonctionnalités demandées sur PocketTracker (C++) :

1. **Page CHAIN (song chain phases)** : à la pression du bouton EDIT (Gesture::A / rôle EDIT) :
   - cellule remplie → le contenu de la cellule (phrase ref + transpose) est copié dans un cache dédié
   - cellule vide → le cache est collé dans la cellule (phrase ref + transpose)
   Ceci remplace le comportement actuel d'EDIT sur CHAIN (actuellement : INSERT de la dernière phrase éditée si vide, rien si remplie).

2. **Page PROJECT > SETTINGS** : une nouvelle ligne de mappage boutons permettant d'affecter les 4 fonctions EDIT, OPTION, PLAY, SHIFT aux 4 boutons physiques A, B, X, Y au choix de l'utilisateur (aujourd'hui hardcodé dans `button_mapper.h` via `role_of()`).

## État actuel

- `native/ui/dispatch/screens/sequencer.cpp` (`chain_screen`, `Gesture::A`) : appelle `open_sub_screen_at_cursor` (faux pour CHAIN) puis `chain_insert()`. `chain_insert()` ne fait rien si la cellule est déjà remplie (`phraseRefs[row] != -1` → return précoce), sinon écrit `lastEditedPhrase` / `lastEditedTranspose`.
- `AppState` (`native/ui/app_state.h`) possède déjà `chainClipboard` (int, -1 = vide) mais c'est un tampon "dernière cellule cliquée" utilisé par `chain_insert`, pas un vrai copier/coller cellule.
- `native/ui/button_mapper.h` : `role_of()` échange dur A↔B ; X et Y sont fixes. Les 4 rôles sont : OPTION=Gesture::B, EDIT=Gesture::A, PLAY=Gesture::X, SHIFT=Gesture::Y.
- `native/ui/settings_row_layout.h` : `SettingsRow` 0..19 (append-only, stocké par numéro dans settings.json). `ABXY = 15` existe déjà (AUTO/XBOX/NINTENDO, ne gère que l'inversion des étiquettes, pas le mappage fonctions→boutons).
- `native/ui/modules/settings_editor.{h,cpp}` : dessine et édite chaque ligne SETTINGS via `cursor_context` / `handle_input`.
- `native/ui/clipboard.h` : copier/coller par sélection (B copie la sélection, Y+A colle) — distinct du cache cellule demandé ici.

## Décisions

### Feature 1 — Copier/coller cellule CHAIN via EDIT

1. Le bouton EDIT est `Gesture::A` (rôle EDIT, bouton physique B par défaut / touche J clavier).
2. Le cache est par écran CHAIN : on réutilise les champs existants `AppState::chainClipboard` (phrase ref) et on ajoute `chainClipboardTranspose` (int, 0 = none) pour le transpose. Pas de nouveau `Clipboard` (celui-ci sert aux sélections multi-cellules).
3. Dans `chain_screen` (`sequencer.cpp`), le `case Gesture::A:` devient :
   - colonne 1 (phrase ref) :
     - si cellule remplie (`phraseRefs[row] >= 0`) → `chainClipboard = phraseRefs[row]`, `chainClipboardTranspose = transposeValues[row]`, status "CHAIN COPIÉ" (succès).
     - si cellule vide (`== -1`) et `chainClipboard >= 0` → écrit `phraseRefs[row] = chainClipboard`, `transposeValues[row] = chainClipboardTranspose`, `mark_modified()`, status "CHAIN COLLÉ" (succès). Sinon status "RIEN DANS LE CACHE" (échec).
   - colonne 2 (transpose) : idem avec la valeur de transpose seule (copie/collage du transpose).
   - colonnes autres : inchangé (passage à `grid_clipboard` via default).
4. `chain_insert()` (INSERT sur cellule vide) est conservé via `Gesture::A_A` (double-tap) uniquement ; le simple A ne fait plus l'insert. L'insert de la dernière phrase éditée se fait désormais par double-tap (déjà le cas) — à vérifier que cela ne casse pas le flux habituel.
5. Le tampon "dernière cellule cliquée" actuel (lignes 126-134 de sequencer.cpp qui mémorise `chainClipboard` au survol) est conservé ou retiré — à décider (recommandé : retirer, remplacé par le copier-coller explicite).

### Feature 2 — Mappage boutons EDIT/OPTION/PLAY/SHIFT dans SETTINGS

1. Nouvelle ligne SETTINGS `BTN_MAP` (row 20, append-only). Elle est toujours visible (pas de gate `caps`).
2. Modèle de données : dans `SettingsValues`, ajouter 4 index (un par fonction) indiquant sur quel bouton physique (0=A,1=B,2=X,3=Y) elle est affectée. Valeur par défaut : EDIT→B(1), OPTION→A(0), PLAY→X(2), SHIFT→Y(3) pour préserver le comportement actuel.
   - Alternative (recommandée) : stocker `int btnForFunction[4]` (fonction → bouton physique) et déduire l'inverse au besoin.
3. Dessin : ligne `param_row`-style montrant les 4 affectations, ex. `"EDIT:B OPTION:A PLAY:X SHIFT:Y"` dans une seule cellule cycle, ou 4 sous-cellules. Recommandé : une seule cellule qui cycle les permutations possibles, ou un écran dédié. Pour la simplicité du premier pas : une cellule par fonction affichant le bouton affecté, navigable avec A+DPAD.
4. Application : le mappage est lu par `button_mapper.h` au lieu de `role_of()` hardcodé. Le `MapperState`/dispatcher expose la configuration (depuis `AppState::settings`) et `handle_button` l'applique avant de traduire le bouton physique en geste.
5. Persistance : `settings.json` (via `SettingsValues`, déjà round-trippé par `ui/settings_store.h`).

## Fichiers concernés

Feature 1 :
- `native/ui/dispatch/screens/sequencer.cpp` : `chain_screen` (case A), `chain_insert` (conserver pour A_A), éventuellement `chain_edit`.
- `native/ui/app_state.h` : ajouter `chainClipboardTranspose` (et renommer/documenter `chainClipboard`).

Feature 2 :
- `native/ui/settings_row_layout.h` : `SettingsRow::BTN_MAP = 20`, `SETTINGS_ROW_COUNT = 21`, ajout dans `SETTINGS_DISPLAY_ORDER`, `settings_row_visible`, `settings_row_has_second_column`, `settings_content_height` (auto via COUNT).
- `native/ui/modules/settings_editor.h` : `SettingsValues` + 4 index de mappage.
- `native/ui/modules/settings_editor.cpp` : dessin (`param_row` ou dédié), `cursor_context` (cycle 0..3 par fonction), `handle_input` (SET_VALUE écrit l'index).
- `native/ui/button_mapper.h` : `role_of()` devient configurable (paramètre ou lecture de l'état du dispatcher via `s_`), suppression du hardcodé A↔B.
- `native/ui/input_dispatcher.h/.cpp` : exposer le mappage courant au `button_mapper` (le `handle_button` template a accès à `d` = dispatcher → `d.settings()`).
- `shell/sdl/sdl-input.cpp` : inchangé (il produit déjà des `Button` physiques ; c'est le mapper qui les traduit).

## Tâches

1. [ ] `app_state.h` : ajouter `int chainClipboardTranspose = 0;` à côté de `chainClipboard`.
2. [ ] `sequencer.cpp` : réécrire `case Gesture::A:` de `chain_screen` pour copier (cellule remplie) / coller (cellule vide) via le cache CHAIN ; garder `chain_insert` pour `Gesture::A_A`.
3. [ ] Compiler + vérifier que l'éditeur de chaîne répond : EDIT sur rempli → "CHAIN COPIÉ", EDIT sur vide → "CHAIN COLLÉ".
4. [ ] `settings_row_layout.h` : ajouter `SettingsRow::BTN_MAP = 20`, mettre à jour `SETTINGS_ROW_COUNT`, `SETTINGS_DISPLAY_ORDER`, `settings_row_visible` (default true), `settings_row_has_second_column` (false).
5. [ ] `settings_editor.h` : ajouter 4 champs de mappage (ex. `int btnEdit = 1, btnOption = 0, btnPlay = 2, btnShift = 3;` où 0=A,1=B,2=X,3=Y).
6. [ ] `settings_editor.cpp` : dessiner la ligne BTN_MAP (4 affectations), `cursor_context` (cycle 0..3), `handle_input` (SET_VALUE).
7. [ ] `button_mapper.h` : remplacer `role_of()` hardcodé par une lecture du mappage configuré (passé depuis le dispatcher / AppState).
8. [ ] `input_dispatcher.h/.cpp` : exposer le mappage `settings` au mapper (le template `handle_button` reçoit `d`).
9. [ ] Compiler + vérifier SETTINGS > BTN_MAP permet de réaffecter les 4 fonctions et que le changement est effectif immédiatement (ex. EDIT sur touche X, PLAY sur touche B).

## Validation

- Build natif réussit (CMake/Gradle selon le setup).
- CHAIN : EDIT sur cellule remplie affiche "CHAIN COPIÉ" ; EDIT sur cellule vide avec cache affiche "CHAIN COLLÉ" et écrit la phrase/transpose ; EDIT sur cellule vide sans cache affiche "RIEN DANS LE CACHE".
- SETTINGS : ligne BTN_MAP visible, modifiable, persiste dans settings.json ; réaffecter EDIT→X puis quitter/relancer applique EDIT sur la touche X.

## Questions ouvertes (à trancher avant l'implémentation)

1. **Feature 1** : le simple EDIT sur cellule remplie devait-il aussi ouvrir l'éditeur de phrase (comportement actuel via `open_sub_screen_at_cursor` qui retourne faux pour CHAIN, donc rien aujourd'hui) ? Oui → on garde l'ouverture en plus du copie ; non → le copie remplace tout (plan suppose "non").
2. **Feature 1** : le cache copie-t-il uniquement la phrase ref (colonne 1) ou aussi le transpose (colonne 2) ? Plan suppose "les deux ensemble" sur la colonne 1, et transpose seul sur la colonne 2.
3. **Feature 2** : UI de la ligne BTN_MAP — une cellule par fonction (4 curseurs) ou une seule cellule listant les 4 affectations ? Plan suppose "une cellule par fonction".
4. **Feature 2** : le mappage s'applique-t-il aussi aux claviers (keyboard bindings) ou seulement aux pads ? Plan suppose "pads uniquement" (le clavier garde son propre `keyMap_`).
