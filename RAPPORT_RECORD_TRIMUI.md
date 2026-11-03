# Rapport de suivi — Record du Sample Editor sur TrimUI Brick (Knulli / PortMaster)

Dernière mise à jour : 31/10/2026 (3e passe, build 18:33 déployé) · Brick : Knulli "scarab 2026/05/11", noyau 4.9.191,
SSH root@<ip de la Brick> (mdp Knulli par défaut : `linux`) · Build : MoonTRCK via `shell/build-moontrck.ps1` (Docker).
> Ce zip est à jour avec tous les correctifs ci-dessous. En cas de changement d'IA : lire ce fichier d'abord.
> ⚠️ Le binaire testé le 31/10 à 16:40 NE contenait PAS le correctif INPUT de la 1re passe (le log n'a pas la ligne
> `audio:   INPUT request ...`). Tout est à rebuilder + redéployer (voir §6).

## 1. Architecture du record
Cellule REC/INPUT (`native/ui/modules/sample_editor.cpp`) → `InputDispatcher::handle_record_toggle()`
(`native/ui/dispatch/screens/sample_editor.cpp`, device = index+1 dans `audioInputDevices`, -1 = défaut) →
`SongcoreHost::start_record` → `AudioEngine::startRecord` → `SdlAudioEngine::openCapture(deviceIndex)`
(`shell/sdl/sdl-audio-engine.cpp`) → `captureCallback` → `AudioEngine::captureWriteImpl` (buffer RAM).
À l'arrêt : `record_capture` installe le PCM (gain logiciel `rec_viz().p.micGainDb`, défaut +12 dB, plage 0..36,
réglage VIZ SETTINGS > MIC GAIN) dans le slot de l'instrument. Lanceur : `shell/portmaster-moontrck/MoonTRCK.sh`.

## 2. Faits mesurés sur la Brick (logs du 31/10)
- Cartes ALSA : 0 = `audiocodec` (micro interne, capture `hw:0,0`), 1 = `H5` (ZOOM H5, USB-Audio, 44,1/48 kHz stéréo).
- SDL tourne en driver **alsa** (`driver=alsa`), lecture via `default` (= PipeWire). Au démarrage de l'app SDL a vu
  **1 seul** périphérique de capture : `audiocodec`. La liste SDL est un instantané pris au lancement : un micro USB
  branché après (le H5 est apparu à t=15254 s, l'app avait ouvert le micro à t≈15218 s) n'y figure jamais.
- Ouverture du micro interne : 12 essais ÉCHOUENT (« Operation not permitted » ; noyau : `invalid rate setting`,
  `hw_params failed -22`) avant que `env:hw:0,0` (2 ch, 48 kHz, S16) réussisse. `arecord -D hw:0,0` accepte pourtant les 4
  formats sans erreur : seul le chemin SDL/plughw est refusé. → délai + rafales d'erreurs dmesg à chaque 1re ouverture.
- Seul le canal gauche (MIC1) est câblé : canal droit rms ≈ 1-5 (silence). `POCKETTRACKER_CAPTURE_LEFT_ONLY=1` correct.
- Réglages codec : `MIC1 gain volume` 0..31 (= 0..31 dB, 1 dB/pas) ; `ADC volume` 0..255, dB = -119,25 + 0,75×valeur
  (160 = +0,75 dB ; 200 = +30,75 dB ; 255 = +72 dB). Le lanceur déployé avait `MIC_GAIN=3` (≈ +4 dB au total !).
- Niveaux (voix forte) : MIC1=12/ADC=160 → rms -48 dBFS, pic 7 % (trop faible) ; MIC1=20/ADC=160 → pic 56 % ;
  MIC1=28/ADC=200 → rms -17 dBFS, pic 56 % (bon niveau). Bruit de fond à 12/160 : rms -51 dBFS. Un « pop » d'ouverture
  (pic ≈ 4,5 % au début) existe : à ignorer ~0,4 s si on analyse.
- ZOOM H5 via `plughw:1,0` : capte bien en stéréo (pics 68 % / 75 %).
- Log MoonTRCK : prises de 15,4 s / 13 s / 48,5 s sans coupure à 3 s (`capture paused (callbacks=… frames=…)`).
  La dernière : 2,56 s, pic 0,0137 → micro quasi muet avec `MIC_GAIN=3`.

## 3. Bugs et état

### Bug 1 — INPUT = USB enregistre quand même le micro interne  → CORRIGÉ (2 causes), À TESTER
a) (1re passe) `openCapture()` reprenait le périphérique déjà ouvert sans regarder le choix → clé de capture
   `captureKey_` : si l'entrée change, fermeture + réouverture. Un choix explicite non interne est STRICT (échec =
   « RECORD FAILED », plus de repli silencieux sur le micro interne).
b) (2e passe) la liste INPUT venait de SDL (instantané au lancement, le USB branché après n'y est pas ; l'entrée 0 était
   renommée à l'aveugle « Micro TrimUI »). Désormais la liste est lue dans `/proc/asound/cards` à chaque ouverture du
   sample editor : `MIC TRIMUI`, `USB H5`, `CARD <id>` (3 premières lettres = ce qu'affiche la cellule : MIC / USB).
   Ouverture : carte interne → `hw:N,0` puis `plughw:N,0` ; carte USB → `plughw:N,0` puis `hw:N,0` (strict).
   Logs : `audio:   input [n] card … -> 'nom'`, `INPUT request`, `INPUT changed (a -> b)`.
   À vérifier : DEFAULT (index ≤ 0) = micro interne.

### Bug 2 — Micro interne trop faible  → CAUSE TROUVÉE (réglages), valeur finale À CALIBRER
Gain total trop bas (MIC_GAIN=3 déployé, 12 dans le dépôt, ADC à +0,75 dB). `MoonTRCK.sh` : défauts provisoires
`MIC_GAIN=24`, `ADC_VOL=190` et lecture de `$GAMEDIR/mic.conf` (`MIC_GAIN=..`, `ADC_VOL=..`) pour régler SANS recompiler.
Reste à choisir le meilleur rapport signal/bruit : `shell/portmaster-moontrck/mic-calibrate.sh` (6 réglages, SNR affiché).
Autres leviers : MIC GAIN logiciel de l'écran VIZ SETTINGS (0..36 dB, appliqué au sample, pas au visualiseur).

### Bug 3 — Le 1er record « se fige à 3 s » jusqu'à PLAY  → CORRIGÉ (cause trouvée), À TESTER
Ce n'était pas la capture (elle continue : 15 s, 13 s, 48 s dans le log) mais l'AFFICHAGE : `shell/app.cpp` n'a un
« idle frame skip » qui ne redessine que s'il y a du son, une entrée, ou un message temporisé ; le statut « RECORDING »
expire après ~3 s, puis plus aucun redessin (transport à l'arrêt = silence, pas d'entrée) jusqu'à PLAY (audible).
Correctif : `recording = state.sampleEditor.recordActive` ajouté aux conditions de dessin ET au `busy` de `pace_tick`.

### Divers (non traités)
- Pop d'ouverture du codec ≈ 0,4 s au début de chaque prise (option : ignorer les premiers ms).
- `help_text.h` ne gère pas `SettingsRow::AUDIO_IN` (warning `-Wswitch`) : pas d'aide pour cette ligne.
- 1er `openCapture` : le watcher PipeWire du lanceur supprime le nœud micro interne toutes les 2 s ; sans conséquence
  mesurée ici (le nœud n'existe pas sous PipeWire sur cette image), à garder.

## 4. Fichiers modifiés dans ce zip
- `shell/sdl/sdl-audio-engine.cpp` / `.h` : bugs 1a et 1b (liste ALSA, clé de capture, ordre des candidats).
- `shell/app.cpp` : bug 3 (2 lignes).
- `shell/portmaster-moontrck/MoonTRCK.sh` : niveaux configurables (`mic.conf`), défauts 24 / 190.
- `shell/portmaster-moontrck/collect-logs.sh`, `mic-calibrate.sh` : outils de diagnostic (nouveaux).
Vérifié ici : syntaxe C++ (`g++ -fsyntax-only`) et `bash -n`. NON compilé en aarch64 ni testé sur la Brick.

## 5. Procédures
Collecte de logs (PowerShell, `ssh ... < fichier` est interdit sous PowerShell) :
`scp collect-logs.sh root@IP:/tmp/` puis `ssh root@IP "tr -d '\r' < /tmp/collect-logs.sh | sh > /tmp/l.txt 2>&1"`
puis `scp root@IP:/tmp/l.txt .`. Le shell de la Brick se lance avec `ssh root@IP` ; les blocs PowerShell (`@' … '@`)
ne se collent QUE sur le PC.
Rebuild + déploiement : `& "$repo\shell\build-moontrck.ps1"` puis `scp "$stage\MoonTRCK.aarch64" root@IP:/userdata/roms/ports/moontrck/`
et `scp "$stage\MoonTRCK.sh" root@IP:/userdata/roms/ports/MoonTRCK.sh` (+ `chmod +x`). Les IP vues : 10.137.175.149 et 10.239.4.149.

## 6. Tests à faire après rebuild (dans cet ordre)
1. Brancher le ZOOM, ouvrir le sample editor : la cellule INPUT propose MIC / USB ; log `audio:   input [..]`.
2. REC 8 s sans toucher à rien : l'écran continue de se mettre à jour au-delà de 3 s (bug 3).
3. INPUT = USB, REC, STOP : le log montre `INPUT request ... STRICT`, `capture using 'env:plughw:1,0'`, signal stéréo du H5.
4. INPUT = MIC, REC : `capture using 'env:hw:0,0'` dès le 1er essai (plus de rafale d'échecs), niveau correct.
5. Trois prises d'affilée (régression historique « crash au 3e record »).

## 7. 3e passe — build 19:48 (md5 `1e6adab1bf1a9b9256133ba5b19f5cb0`)

### 7.1 REC sur les bus internes (P1..P8 / M1 MASTER)
- `AudioEngine::listInputDevices()` ajoute **toujours en fin de liste** `P1`…`P8` puis `M1 MASTER`
  (`INTERNAL_INPUT_COUNT = 9`, codes `INTERNAL_INPUT_BASE = 100 + n`) : le sample editor mappe l'index de la liste
  (cartes physiques = `i+1`, bus = `100 + i - phys`).
- `AudioEngine::startRecord()` : un code ≥ 100 ne touche PAS à ALSA (aucun `openCapture`, donc pas de micro) ;
  il arme `recordTapTrack` et le thread audio recopie le bus dans le même buffer que la capture externe
  (`captureWriteImpl`) → visualiseur, niveau, `COUNT-IN`, `installRecord` inchangés.
- P1..P8 = sortie **sèche** de la piste (fader + mute appliqués, avant master, sans reverb/delay) ; M1 = ce qui
  sort du master (chaîne master + clic de décompte). Les accumulateurs OCTA sont réutilisés pendant la prise
  (`clearBlockScratch` les zérote aussi quand `recordTap` vise une piste).
- `handle_record_toggle()` appelle `refresh_input_devices()` avant de démarrer (nom sauvegardé / micro branché
  après le boot → toujours mappé).

### 7.2 Menu SET séparé et déroulant
- Deux sections : `VIZ` (GATE, TIME, BASS, TRAIL, AXIS, SPREAD) et `SAMPLE / REC` (MIC GAIN, START, CLICK, DURATION).
- Fenêtre déroulante : `fit = (h - 12) / rowH` lignes visibles (7 sur la Brick), elle suit le curseur, barre de
  défilement à droite. Le menu tient désormais dans le panneau (le redessin de fin de `draw()` a été retiré).
- SPREAD : plage étendue à **0.5 … x8.0**, pas 0.2 (A+←/→ = 1.0) ; la borne de relecture dans `settings.json` suit.

### 7.3 Persistance des réglages
- `settings.json` (= `$GAMEDIR/data/settings.json`, `POCKETTRACKER_HOME`) gagne `record_input` (nom du périphérique
  de la cellule INPUT) et l'objet `rec` : axis, gate, time, bass, trail, spread, mic_gain, start, click, duration.
- Écrit à la sortie / mise en arrière-plan (`save_settings_if_changed`, comparaison d'octets) ;
  relu au boot par `load_settings` (bornes = celles du menu). `AppState.sampleEditor.recordInputDevice` est
  recopié dans `settings.recordInput` avant chaque sauvegarde et restauré au lancement.
- Conséquence : plus besoin de refaire les réglages à chaque lancement. (Des presets nommés restent à faire si besoin.)

