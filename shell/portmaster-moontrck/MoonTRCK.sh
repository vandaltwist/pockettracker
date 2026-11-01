#!/bin/bash
#
# MoonTRCK - PortMaster launch script.
#
# =============================================================================================
#  DO NOT ADD gptokeyb TO THIS FILE.
# =============================================================================================
#
# See shell/portmaster/PocketTracker.sh for the rationale: MoonTRCK reads the pad itself through
# SDL's GameController API, so gptokeyb injects every press a second time and breaks START/stop.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/moontrck

cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export POCKETTRACKER_HOME="$GAMEDIR/data"

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# --- Micro interne du TrimUI Brick (codec Allwinner "audiocodec") -------------------------------
# Les deux interrupteurs "Boost" ACTIVENT l'entree micro : sans eux, la capture echoue en I/O error.
# Seul MIC1 (canal gauche) est cable ; MIC2 reste muet.
# Niveau (mesure sur la Brick, 31/10/2026) : MIC1 gain = 0..31 dB (analogique), ADC volume = 0..255,
# 160 = +0,75 dB, 0,75 dB par pas (200 = +30,75 dB). Reglable SANS recompiler : creer
# $GAMEDIR/mic.conf contenant par exemple  MIC_GAIN=24  et  ADC_VOL=190  (une ligne chacun).
[ -f "$GAMEDIR/mic.conf" ] && . "$GAMEDIR/mic.conf"
if command -v amixer >/dev/null 2>&1 && grep -q audiocodec /proc/asound/cards 2>/dev/null; then
  MIC_GAIN=${MIC_GAIN:-24}
  ADC_VOL=${ADC_VOL:-190}
  # PipeWire garde la source micro interne ouverte (nœud alsa_input...sndcodec...) et la configure mal
  # (noyau : "invalid rate setting", donc aucune donnee), ce qui bloque aussi l'acces direct (EBUSY).
  # On supprime ce nœud (seulement celui du codec interne, pas un micro USB) : la carte est libre et
  # MoonTRCK lit audiocodec directement. Un veilleur le supprime a nouveau s'il revient pendant la session.
  # La lecture du son (sink) n'est pas touchee.
  if command -v pw-cli >/dev/null 2>&1; then
    pw_free_mic() {
      for id in $(pw-cli ls Node 2>/dev/null | awk '/^[[:space:]]*id [0-9]+,/ {gsub(",","",$2); id=$2} /node\.name = "alsa_input.*sndcodec/ {print id}'); do
        pw-cli destroy "$id" >/dev/null 2>&1
      done
    }
    pw_free_mic
    sleep 0.3
    ( while sleep 2; do pw_free_mic; done ) &
    PW_WATCH=$!
  fi
  amixer -c audiocodec -q cset name='ADCL Input MIC1 Boost Switch' on
  amixer -c audiocodec -q cset name='ADCR Input MIC2 Boost Switch' on
  amixer -c audiocodec -q cset name='MIC1 gain volume' "$MIC_GAIN"
  amixer -c audiocodec -q cset name='MIC2 gain volume' "$MIC_GAIN"
  amixer -c audiocodec -q cset name='ADC volume' "$ADC_VOL,$ADC_VOL"
  export POCKETTRACKER_CAPTURE_LEFT_ONLY=1
fi

chmod +x "$GAMEDIR/MoonTRCK.${DEVICE_ARCH}" 2>/dev/null || true

pm_platform_helper "$GAMEDIR/MoonTRCK.${DEVICE_ARCH}"

./MoonTRCK.${DEVICE_ARCH}

[ -n "$PW_WATCH" ] && kill "$PW_WATCH" 2>/dev/null

pm_finish
