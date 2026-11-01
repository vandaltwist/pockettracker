# Plan — cellule VIZ "SET" cliquable dans le sample editor

## But
Renommer le bouton "J:SET" du visualiseur en "SET" et le rendre cliquable pour ouvrir la page de paramètres du viz (menu overlay GATE/TIME/BASS/TRAIL/AXIS/AUTO CAL déjà présent dans `rec_viz.h`).

## État actuel
- `rec_viz.h` possède déjà un menu de paramètres (lignes 97-121, 572-604) : `MENU_N = 6`, labels GATE/TIME/BASS/TRAIL/AXIS/AUTO CAL, struct `Params`, `toggleMenu()`/`menuMove()`/`menuAdjust()`/`click()`.
- Le bouton est dessiné par `RecViz::draw()` (lignes 572-580) avec le label `"J:SET"`, sa zone cliquable est enregistrée dans `btn_[4]` et `click(mx, my)` bascule le menu.
- `SampleEditorModule::draw_rec_viz` (sample_editor.cpp:618) appelle seulement `v.draw(...)` — aucun branchement de clic sur le bouton SET n'est fait depuis l'éditeur/dispatcher.
- Il n'y a pas de cellule persistante dans la grille du sample editor pour ce bouton ; il est overlappé en haut à droite du panneau L pendant le REC.

## Décisions
1. Renommer le label `"J:SET"` → `"SET"` dans `rec_viz.h` (ligne 574). La largeur du bouton se recalcule automatiquement via `tw("SET")`.
2. Garder le bouton overlappé (pas de nouvelle cellule de grille) : cliquer dedans ouvre/ferme le menu viz.
3. Brancher le clic : dans le dispatcher de l'éditeur, quand un clic/tap tombe dans la zone `btn_` de `rec_viz`, appeler `rec_viz().click(mx, my)` (qui retourne true si le bouton est touché et bascule le menu). Quand le menu viz est ouvert, les flèches D-pad doivent piloter `menuMove`/`menuAdjust` du viz (le curseur éditeur est gelé).
4. Le menu viz reste accessible en permanence (pas seulement pendant le REC), car `v.draw()` dessine le bouton quoi qu'il arrive.

## Fichiers concernés
- `native/ui/modules/rec_viz.h` : renommer le label, vérifier `btn_`/`click`.
- `native/ui/modules/sample_editor.cpp` : éventuellement exposer la zone de clic dans `draw_rec_viz`.
- `native/ui/dispatch/screens/sample_editor.cpp` : brancher `rec_viz().click()` sur l'input clic/tap ; geler le curseur éditeur quand le menu viz est ouvert.

## Tâches
1. [ ] `rec_viz.h` : remplacer `"J:SET"` par `"SET"` ; s'assurer que `click()` reste cohérent avec la nouvelle largeur (`tw("SET")`).
2. [ ] `sample_editor.cpp` : dans `draw_rec_viz`, exposer la zone cliquable du bouton SET (ou ré-exposer `btn_`) pour que le dispatcher puisse tester le clic.
3. [ ] `dispatch/screens/sample_editor.cpp` : ajouter la gestion du clic sur la zone SET → `rec_viz().toggleMenu()` ; quand `rec_viz().menuOpen()` est true, rediriger les flèches D-pad vers `menuMove`/`menuAdjust` et geler le curseur éditeur.
4. [ ] Vérifier que le menu s'affiche bien en overlay par-dessus l'éditeur quand on clique sur "SET".

## Validation
- Compiler le projet (Gradle / CMake selon le setup natif).
- Lancer l'éditeur d'échantillon, cliquer sur "SET" : le menu "VIZ SETTINGS" apparaît avec les 6 lignes.
- Flèches haut/bas déplacent la sélection ; gauche/droite modifient les valeurs.
- Re-cliquer sur "SET" ferme le menu.

## Notes / risques
- `rec_viz.Params` est interne au viz et n'est pas partagé avec `SampleEditorState` (les champs `recViz*` de l'état ne sont pas câblés). Ce plan ne les câble pas : le menu viz modifie directement `rec_viz.p`.
- Pas de sauvegarde/persistance des params viz dans ce périmètre.
