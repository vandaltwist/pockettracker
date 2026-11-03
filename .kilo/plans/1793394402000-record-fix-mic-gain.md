# Plan — Correction du REC sample editor (arrêt, lecture+capture, blocage 3s, cellule SET)

## But

Corriger quatre problèmes dans l'écran SAMPLE EDITOR (PocketTracker, cible TrimUI Brick / Knulli aarch64) :
1. **Le REC s'arrête tout seul** — l'enregistrement s'interrompt avant que l'utilisateur ne stoppe explicitement. Recliquer relance un nouvel enregistrement par-dessus le précédent.
2. **Conflit lecture+capture** — quand l'enregistrement est actif sur le micro interne (audiocodec), les autres sources de lecture (M8 USB Audio, etc.) ne s'entendent plus.
3. **Blocage à 3s** — l'enregistrement se bloque à 3s systématiquement, y compris sur la version PC.
4. **Cellule SET dans le sample record** — les options de démarrage (DIRECT, 1/4, 2/4, 1 BAR, 2 BAR) sont accessibles via la cellule SET du sample editor (pas un nouveau menu START séparé).

Le visualiseur (courbes de niveau L/R) est "absolument parfait" : **ne pas y toucher**.
L'étalonnage du silence (autoCal) et le gain micro (micGainDb) sont retirés du périmètre (demande utilisateur : "oublie ça").

## Décisions

| Décision | Rationale |
|---|---|
| `stop_record()` uniquement sur action utilisateur explicite | `handle_record_toggle()` appelle `stop_record()` avant de démarrer un nouvel enregistrement — acceptable. Mais `close_sample_editor()` et le destructeur appellent aussi `stop_record()` — vérifier qu'aucun appel intempestif n'arrête le REC en cours. |
| Conflit lecture+capture à résoudre dans `shell/sdl/sdl-audio-engine.cpp` | Ouvrir la capture sur `plughw`/`default` (mixage ALSA via dmix) plutôt que `hw` (accès exclusif), et s'assurer que le périphérique de lecture reste ouvert indépendamment de la capture. |
| Cellule SET du sample editor | Les options DIRECT / 1/4 / 2/4 / 1 BAR / 2 BAR sont proposées dans la cellule SET existante du sample editor, qui configure `startCountIn` au démarrage du REC. |
| Visualiseur : zéro modification | `rec_viz().feedBuffer()` reste appelé pendant le REC. Aucun changement de rendu. |

## Fichiers concernés

1. `native/ui/dispatch/screens/sample_editor.cpp` — `handle_record_toggle()` (ligne 1109) : s'assurer que `stop_record()` n'est appelé que dans la branche d'arrêt explicite. Ajouter les options de démarrage dans la cellule SET du REC.
2. `native/ui/modules/sample_editor.h` — ajouter l'état lié à la cellule SET (startMode, ou réutiliser `rec_viz().p.startMode`).
3. `native/songcore/host.cpp` — `record_capture()`, `start_record()`, `stop_record()` déjà câblés. Vérifier qu'aucun appel caché ne stoppe le capture prématurément.
4. `native/audio-engine.cpp` — `installRecord()` applique `gainDb`. Vérifier `startRecord()` (alloue `SAMPLE_RATE * 60` frames, pas de limite à 3s).
5. `shell/sdl/sdl-audio-engine.cpp` — capturer sur `plughw`/`default` au lieu de `hw` pour ne pas verrouiller la lecture des autres sources. Corriger l'arrêt prématuré des callbacks capture (blocage 3s).

## Tâches

1. **[sample_editor.cpp]** Auditer `handle_record_toggle()` : `stop_record()` uniquement dans la branche d'arrêt explicite (`se.recordActive` true). Vérifier que `record_capture()` ne laisse pas `recordActive` incohérent en cas d'échec d'`installRecord`.
2. **[sample_editor.cpp + sample_editor.h]** Ajouter les options de démarrage (DIRECT / 1/4 / 2/4 / 1 BAR / 2 BAR) dans la cellule SET du sample record, qui configure `startCountIn` au démarrage du REC.
3. **[shell/sdl/sdl-audio-engine.cpp]** Résoudre le conflit lecture+capture : ouverture de la capture sur `plughw`/`default` plutôt que `hw`, périphérique de lecture resté ouvert indépendamment de la capture.
4. **[shell/sdl/sdl-audio-engine.cpp]** Identifier et corriger le blocage à 3s de l'enregistrement (sur PC et TrimUI). Aucun code ne limite à 3s (`MAX_RECORD_SECONDS = 60`). Vérifier timeout device, remplissage buffer, arrêt prématuré des callbacks capture.
5. **[Déploiement]** Redéployer le binaire aarch64 + le `MoonTRCK.sh` patché sur la TrimUI.
6. **[Validation]** Reconstruire via le build Docker, déployer, et tester : REC ne s'arrête pas tout seul, lecture des autres sources audible pendant l'enregistrement, enregistrement ne se bloque pas à 3s, cellule SET du sample record avec les options de démarrage fonctionne.

## État des dépendances

- Le patch `capture-fix-v2.patch` a été appliqué sur les sources et le build Docker a réussi (`MoonTRCK.aarch64` valide).
- Le lanceur `MoonTRCK.sh` sur l'appareil n'est pas encore patché (bloc amixer audiocodec manquant).
- `rec_viz.h` possède déjà `startMode` (case 6) et `clickOn` (case 7) — réutilisables ou à recâbler vers la cellule SET du sample record.

## Validation

- Build Docker réussit (binaire ELF aarch64).
- Sur la TrimUI : REC démarre et ne s'arrête que sur second appui REC. Les autres sources de lecture restent audibles pendant l'enregistrement. L'enregistrement ne se bloque pas à 3s. La cellule SET du sample editor propose DIRECT / 1/4 / 2/4 / 1 BAR / 2 BAR et fonctionne.
- Le visualiseur affiche les courbes L/R normalement pendant le REC (inchangé).

## Rollback

- Le visualiseur, `startMode`, `clickOn` sont intacts. Aucun changement sur `rec_viz.h` hors périmètre.

## Questions ouvertes

1. **Arrêt inattendu** : un message d'erreur dans `log.txt` quand le REC s'arrête tout seul ? (ex: "capture open failed", "device busy", segfault)
2. **Lanceur sur l'appareil** : redéployer `MoonTRCK.sh` patché en même temps que le binaire ?
