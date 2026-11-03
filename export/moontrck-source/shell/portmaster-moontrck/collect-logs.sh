#!/bin/sh
# Collecte TOUT en une passe (Knulli / TrimUI Brick). A lancer juste APRES avoir reproduit les bugs,
# SANS relancer MoonTRCK (log.txt est vide a chaque lancement).
# Depuis le PC :  ssh root@10.239.4.149 'sh -s' < collect-logs.sh > logs-brick.txt 2>&1
H(){ printf '\n\n######## %s ########\n' "$1"; }
H "DATE / SYSTEME";      date; uname -a; cat /etc/os-release 2>/dev/null | head -5; uptime
H "LOG MOONTRCK (log.txt)"
for f in $(find /userdata /roms /mnt -maxdepth 5 -path '*moontrck*' -name log.txt 2>/dev/null | head -3); do echo "== $f"; tail -n 250 "$f"; done
H "CARTES ET PERIPH. AUDIO"; cat /proc/asound/cards; arecord -l 2>&1; arecord -L 2>&1 | head -40; aplay -l 2>&1
H "USB"; lsusb 2>&1; for c in /proc/asound/card*; do echo "-- $c: $(cat $c/id 2>/dev/null)"; done
H "ETAT PCM CAPTURE/LECTURE (hw_params/status)"
for f in /proc/asound/card*/pcm*/sub*/hw_params /proc/asound/card*/pcm*/sub*/status; do echo "== $f"; cat "$f" 2>&1 | head -12; done
H "MIXER audiocodec (tous les controles + valeurs)"; amixer -c audiocodec contents 2>&1
H "MIXER autres cartes"; for n in 1 2 3; do amixer -c $n contents 2>&1 | head -40; done
H "PIPEWIRE / PULSE"; ps | grep -Ei 'pipewire|wireplumber|pulse|MoonTRCK|pw-cli' | grep -v grep
wpctl status 2>&1 | head -50; pw-cli ls Node 2>&1 | grep -E 'id |node.name|media.class' | head -60
H "ENV AUDIO / SDL"; env | grep -Ei 'SDL|AUDIO|POCKET|MIC_GAIN|PIPEWIRE|ALSA'
H "ASOUND CONF"; cat /etc/asound.conf ~/.asoundrc 2>/dev/null
H "DMESG (audio/usb/sunxi)"; dmesg 2>&1 | grep -Ei 'snd|alsa|audio|usb|codec|sunxi|xrun|underrun|overrun|oom|killed' | tail -n 80
H "MEMOIRE"; free -m; df -h /userdata 2>/dev/null | tail -2
echo; echo "FIN COLLECTE"
