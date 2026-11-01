#!/bin/sh
# Calibration du micro interne (Brick). A coller/lancer dans le shell de la Brick, MoonTRCK ferme.
# Pour chaque reglage : 3 s de SILENCE puis 5 s ou tu PARLES NORMALEMENT a ~50 cm (compte 1,2,3...).
# Sortie : niveau du bruit, niveau de la voix, et l'ecart (SNR). On garde le reglage au meilleur compromis.
amixer -c audiocodec -q cset name='ADCL Input MIC1 Boost Switch' on
amixer -c audiocodec -q cset name='ADCR Input MIC2 Boost Switch' on
cat > /tmp/lvl.py <<'PY'
import sys,wave,array,math
w=wave.open(sys.argv[1]); n=w.getnchannels(); r=w.getframerate()
a=array.array('h'); a.frombytes(w.readframes(w.getnframes()))
s=a[0::n][int(r*0.4):]          # canal MIC1, on saute les 0,4 s du "pop" d'ouverture
rms=math.sqrt(sum(x*x for x in s)/len(s)); pk=max(abs(x) for x in s)
db=lambda v: 20*math.log10(max(v,1)/32768.0)
print("%.1f %.1f %.1f"%(db(rms),db(pk),pk/327.68))
PY
for combo in "12 160" "24 160" "28 160" "20 200" "24 190" "28 200"; do
  set -- $combo
  amixer -c audiocodec -q cset name='MIC1 gain volume' $1
  amixer -c audiocodec -q cset name='ADC volume' $2,$2
  echo; echo "=== MIC1=$1 ADC=$2 : SILENCE 3 s (ne parle pas, ne touche a rien) ==="; sleep 1
  arecord -D hw:0,0 -f S16_LE -c 2 -r 48000 -d 3 /tmp/s.wav 2>/dev/null
  set -- $combo
  echo "=== PARLE normalement dans 2 s (5 s) ==="; sleep 2
  arecord -D hw:0,0 -f S16_LE -c 2 -r 48000 -d 5 /tmp/v.wav 2>/dev/null
  N=$(python3 /tmp/lvl.py /tmp/s.wav); V=$(python3 /tmp/lvl.py /tmp/v.wav)
  python3 - "$N" "$V" "$1" "$2" <<'PY'
import sys
n=[float(x) for x in sys.argv[1].split()]; v=[float(x) for x in sys.argv[2].split()]
print("MIC1=%s ADC=%s | bruit rms %.1f dBFS | voix rms %.1f dBFS pic %.1f%% | SNR %.1f dB"%(sys.argv[3],sys.argv[4],n[0],v[0],v[2],v[0]-n[0]))
PY
done
amixer -c audiocodec -q cset name='MIC1 gain volume' 12
amixer -c audiocodec -q cset name='ADC volume' 160,160
echo; echo FIN
