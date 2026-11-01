# Plan: Bouton Record dans l'éditeur d'échantillon (sample editor)

## But
Ajouter une fonction Record dans l'écran SAMPLE EDITOR (page edit de l'instrument sample). Un bouton REC permet de capturer l'entrée audio sélectionnée, avec un sélecteur de source d'entrée (INPUT). Le résultat devient un nouvel échantillon éditable immédiatement.

## Contexte technique
- Pas de capture audio existante dans le codebase.
- L'éditeur d'échantillon : `native/ui/modules/sample_editor.{h,cpp}`, état `SampleEditorState` dans `native/ui/app_state.h`.
- Ouvert depuis INSTRUMENT via `InputDispatcher::open_sample_editor()`.
- Le moteur audio : `native/audio-engine.{h,cpp}` (pas d'entrée micro actuelle).
- Backends shell : SDL (desktop), Oboe (Android), WASAPI/ASIO (Windows).

---

## 1. Modèle de données

### `native/ui/app_state.h` — struct `AppState`
Ajouter :
- `std::vector<std::string> audioInputDevices;` — noms des périphériques d'entrée
- `int audioInputIndex = 0;` — index sélectionné
- `bool recordInProgress = false;` — état record (runtime)
- `int recordDeviceIndex = -1;` — périphérique choisi pour le record

### `native/ui/modules/settings_editor.h` — struct `SettingsValues`
Ajouter :
- `std::string audioInputDevice = "OFF";`
- `int audioInputIndex = 0;`
- `int audioInputCount = 1;`

### `native/ui/modules/sample_editor.h` — struct `SampleEditorState`
Ajouter un mode/état record :
- `bool recordMode = false;` — l'éditeur est en mode record
- `int recordInputIndex = -1;` — périphérique d'entrée pour ce record

---

## 2. UI du sample editor — mode record

### `native/ui/modules/sample_editor.cpp`
- Ajouter un dessin de la barre/ligne RECORD (REC + INPUT + niveau d'entrée).
- `REC` : affiche "REC" / "REC !" (en cours) / "STOP".
- `INPUT` : affiche le nom du périphérique sélectionné ou "OFF".
- Niveau d'entrée en temps réel (barre ou chiffre) pendant le record.
- Durée enregistrée.

### `native/ui/modules/sample_editor.h`
- Rien de structurel supplémentaire si l'état vit dans `SampleEditorState`.

---

## 3. Dispatcher

### `native/ui/dispatch/screens/sample_editor.cpp`
- `sample_editor_screen()` : gérer A sur la cellule REC (démarrer/arrêter le record).
- `open_sample_editor()` : ajouter un paramètre ou un chemin pour ouvrir en mode record directement.
- À l'arrêt du record : injecter le PCM capturé dans un nouveau slot d'échantillon, l'instrument courant pointe dessus, rester en mode édition.

### `native/ui/input_dispatcher.h` / `.cpp`
- `open_sample_editor()` supporter une ouverture en mode record.

---

## 4. Capture audio (nouveau)

### `native/audio-engine.{h,cpp}`
- `startRecord(int deviceIndex, int sampleRate, int channels)` — ouvre l'entrée, remplit un buffer circulaire.
- `stopRecord()` — arrête, rend le PCM disponible.
- `recordedBuffer()` — retourne float L/R + longueur en frames.
- `listInputDevices()` — remplit `AppState::audioInputDevices`.

### Backends shell
- `shell/sdl/sdl-audio-engine.{h,cpp}` — `SDL_OpenAudioDevice` capture.
- `shell/android/oboe-audio-engine.{h,cpp}` — Oboe input.
- `shell/windows/wasapi-audio-engine.{h,cpp}` / `asio-audio-engine.{h,cpp}` — capture Windows.
- `shell/linux/alsa-route.{h,cpp}` — ALSA (optionnel).

---

## 5. SETTINGS — périphérique d'entrée

### `native/ui/modules/settings_editor.cpp`
- Ligne AUDIO IN (masquée si `PlatformCaps` n'a pas d'entrée).
- Cycle sur `audioInputCount`.

### `native/ui/modules/settings_row_layout.h`
- Ajouter la ligne si nécessaire.

---

## 6. Injection du PCM capturé

### `native/songcore/host.h` / `.cpp`
- `loadSampleFromBuffer()` ou réutiliser `AudioEngine::setSampleBuffers()` pour créer un nouvel échantillon depuis le buffer record.
- L'instrument courant pointe sur ce nouveau slot.

---

## 7. Flux utilisateur cible

1. Écran INSTRUMENT, instrument type SAMPLER, appuyer sur EDIT (SOURCE_EDIT) → ouvre SAMPLE EDITOR.
2. Dans le sample editor, nouvelle zone RECORD : choisir INPUT (périphérique), appuyer sur REC.
3. L'enregistrement commence (niveau affiché, "REC !").
4. Appuyer sur REC (ou B) → arrêt.
5. Le buffer capturé devient un nouvel échantillon ; l'éditeur bascule en mode édition normale sur cet échantillon.

---

## 8. Questions ouvertes

1. **Backends shell avec capture ?** Au minimum SDL. Android/Oboe et Windows WASAPI à confirmer.
2. **Format ?** Stéréo/mono, 16-bit, 48 kHz (taux moteur).
3. **Sauvegarde ?** En RAM jusqu'au SAVE de l'éditeur, ou fichier `Record_NNNN.wav` dans `Samples/`.
4. **Durée max ?** Limite RAM (Miyoo 128 Mo) — prévoir une limite ou arrêt auto.
5. **Nouvel écran ScreenType ou mode du sample editor existant ?** Recommandé : mode du sample editor existant.

---

## Validation

- `cmake --build shell/build --config Release` réussit.
- L'appli se lance, SAMPLE EDITOR ouvert, zone RECORD visible.
- Sélection d'un périphérique d'entrée dans SETTINGS > AUDIO IN.
- Appui sur REC capte l'entrée, nouvel échantillon éditable.
