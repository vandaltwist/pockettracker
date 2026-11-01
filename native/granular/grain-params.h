// grain-params.h -- GRAINSYNTH : parametres partages (SETGRANULAR + moteur temps reel).
// Header seul, aucune dependance au reste de PocketTracker. Sert a :
//   - l'effet hors-ligne SETGRANULAR (grain-fx.h)
//   - la voix temps reel de l'instrument GRAINSYNTH (grain-synth.h)
//   - l'ecran SETGRANULAR (ui/modules/setgranular.h) : la table PARAM_INFO le dessine.
// Pour AJOUTER un effet : une ligne dans enum Param + une ligne dans PARAM_INFO + son code dans grain-fx.h.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace grainfx {

enum Param : int {
    // GRAIN
    P_SIZE, P_DENS, P_POS, P_SCAN, P_PITCH, P_SPRAY, P_PJIT, P_REV,
    // WINDOW
    P_SHAPE, P_SKEW,
    // TEXTURE
    P_FREEZE, P_STUTR, P_SKIP, P_OCT, P_FIFTH, P_QUANT,
    // SPACE
    P_WIDTH, P_DLYT, P_DLYFB,
    // COLOR
    P_FOLLOW, P_GATE, P_CRUSH, P_DRIVE, P_LOCUT, P_HICUT,
    // OUT
    P_MIX, P_LEVEL, P_SEED,
    P_COUNT
};

// live = lu aussi par la voix temps reel de l'instrument (les autres : APPLY hors-ligne seulement).
struct ParamInfo { const char* label; const char* group; uint8_t def; bool live; };

inline constexpr ParamInfo PARAM_INFO[P_COUNT] = {
    {"SIZE",  "GRAIN",   0x60, false},   // 5..500 ms (l'instrument lit sa propre taille : ins.granular)
    {"DENS",  "GRAIN",   0x90, false},   // 1..200 grains/s
    {"POS",   "GRAIN",   0x00, false},   // point de lecture dans l'echantillon
    {"SCAN",  "GRAIN",   0x80, false},   // 80 = fige, >80 avance, <80 recule
    {"PITCH", "GRAIN",   0x80, false},   // 80 = 0, +-24 demi-tons
    {"SPRAY", "GRAIN",   0x20, true },   // dispersion aleatoire de la position
    {"PJIT",  "GRAIN",   0x00, true },   // dispersion aleatoire de la hauteur
    {"REV",   "GRAIN",   0x00, true },   // probabilite de grain inverse
    {"SHAPE", "WINDOW",  0x00, true },   // 0 HANN 1 TRI 2 FLAT 3 GAUSS 4 PERC 5 SWELL
    {"SKEW",  "WINDOW",  0x80, true },   // position du pic de la fenetre
    {"FREEZE","TEXTURE", 0x00, true },   // >=80 : tete de lecture figee
    {"STUTR", "TEXTURE", 0x00, true },   // probabilite de repeter la position precedente
    {"SKIP",  "TEXTURE", 0x00, true },   // probabilite de grain saute (trous)
    {"OCT",   "TEXTURE", 0x00, true },   // probabilite de saut d'octave
    {"FIFTH", "TEXTURE", 0x00, true },   // probabilite de +7 demi-tons (shimmer)
    {"QUANT", "TEXTURE", 0x00, true },   // 0 OFF 1 CHRO 2 MAJ 3 MIN 4 PENTA 5 WHOLE 6 OCT 7 5TH
    {"WIDTH", "SPACE",   0x40, true },   // decorrelation stereo
    {"DLYT",  "SPACE",   0x60, false},   // temps du delay de nuage
    {"DLYFB", "SPACE",   0x00, false},   // feedback du delay (0 = off)
    {"FOLLOW","COLOR",   0x00, false},   // suit l'enveloppe du son d'origine
    {"GATE",  "COLOR",   0x00, false},   // hachage aleatoire
    {"CRUSH", "COLOR",   0x00, false},   // reduction de bits
    {"DRIVE", "COLOR",   0x00, false},   // saturation
    {"LOCUT", "COLOR",   0x00, false},   // passe-haut (0 = off)
    {"HICUT", "COLOR",   0x00, false},   // passe-bas  (0 = off)
    {"MIX",   "OUT",     0xFF, false},   // sec/traite
    {"LEVEL", "OUT",     0x80, false},   // 80 = gain 1
    {"SEED",  "OUT",     0x00, true },   // graine du hasard
};

struct Params {
    uint8_t v[P_COUNT];
    Params() { reset(); }
    void reset() { for (int i = 0; i < P_COUNT; ++i) v[i] = PARAM_INFO[i].def; }
};

// ---- Valeurs physiques -------------------------------------------------------------------------
inline float sizeMs(int v)    { return 5.0f * std::pow(100.0f, v / 255.0f); }
inline float densHz(int v)    { return std::pow(200.0f, v / 255.0f); }
inline float semis(int v)     { return (v - 128) * (24.0f / 128.0f); }
inline float prob(int v)      { return v / 255.0f; }
inline float scanSpeed(int v) { const float k = (v - 128) / 128.0f; return k * std::fabs(k) * 2.0f; }  // images/echantillon
inline float dlyMs(int v)     { return 10.0f * std::pow(50.0f, v / 255.0f); }

inline const char* shape_name(int v) {
    static const char* n[6] = {"HANN", "TRI", "FLAT", "GAUSS", "PERC", "SWELL"};
    return n[(v * 6) / 256 > 5 ? 5 : (v * 6) / 256];
}
inline const char* quant_name(int v) {
    static const char* n[8] = {"OFF", "CHRO", "MAJ", "MIN", "PENTA", "WHOLE", "OCT", "5TH"};
    return n[(v * 8) / 256 > 7 ? 7 : (v * 8) / 256];
}

// Texte affiche dans la boite SETGRANULAR (largeur <= 6 caracteres).
inline std::string format(int p, int v) {
    char b[24];
    switch (p) {
        case P_SHAPE: return shape_name(v);
        case P_QUANT: return quant_name(v);
        case P_FREEZE: return v >= 128 ? "ON" : "OFF";
        case P_PITCH:  std::snprintf(b, sizeof b, "%+.1f", semis(v)); return b;
        case P_SIZE:   std::snprintf(b, sizeof b, "%dMS", (int)sizeMs(v)); return b;
        case P_DENS:   std::snprintf(b, sizeof b, "%dHZ", (int)densHz(v)); return b;
        default:       std::snprintf(b, sizeof b, "%02X", v & 0xFF); return b;
    }
}

// ---- Fenetres de grain (table commune a l'effet et a la voix) -----------------------------------
// Construite au demarrage du programme (variable inline C++17) : rien n'est alloue sur le thread audio.
struct WindowBank {
    static constexpr int SHAPES = 6, N = 512;
    float t[SHAPES][N + 1];
    WindowBank() {
        const double PI2 = 6.283185307179586;
        for (int s = 0; s < SHAPES; ++s)
            for (int i = 0; i <= N; ++i) {
                const double x = (double)i / N;
                double w = 0.0;
                switch (s) {
                    case 0: w = 0.5 - 0.5 * std::cos(PI2 * x); break;
                    case 1: w = 1.0 - std::fabs(2.0 * x - 1.0); break;
                    case 2: w = x < 0.2 ? 0.5 - 0.5 * std::cos(PI2 * 0.5 * x / 0.2)
                              : x > 0.8 ? 0.5 - 0.5 * std::cos(PI2 * 0.5 * (1.0 - x) / 0.2) : 1.0; break;
                    case 3: { const double d = (x - 0.5) / 0.18; w = std::exp(-0.5 * d * d); } break;
                    case 4: w = (x < 0.02 ? x / 0.02 : 1.0) * std::exp(-5.0 * x); break;
                    default: { const double y = 1.0 - x; w = (y < 0.02 ? y / 0.02 : 1.0) * std::exp(-5.0 * y); } break;
                }
                t[s][i] = (float)w;
            }
    }
};
inline const WindowBank WINDOWS{};

// phase 0..1 dans le grain -> gain. `skew` (0..255, 80 = centre) deplace le pic.
inline float window_at(int shapeByte, int skewByte, float phase) {
    const int   s  = (shapeByte * WindowBank::SHAPES) / 256 > WindowBank::SHAPES - 1
                         ? WindowBank::SHAPES - 1 : (shapeByte * WindowBank::SHAPES) / 256;
    float pk = 0.5f + (skewByte - 128) * (0.4f / 128.0f);
    pk = pk < 0.1f ? 0.1f : (pk > 0.9f ? 0.9f : pk);
    const float x = phase < pk ? 0.5f * phase / pk : 0.5f + 0.5f * (phase - pk) / (1.0f - pk);
    const float f = (x < 0.f ? 0.f : (x > 1.f ? 1.f : x)) * WindowBank::N;
    const int   i = (int)f;
    const float a = WINDOWS.t[s][i], b = WINDOWS.t[s][i + 1 > WindowBank::N ? WindowBank::N : i + 1];
    return a + (b - a) * (f - (float)i);
}

// Hasard deterministe (xorshift32) : meme graine = meme rendu, hors-ligne comme temps reel.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; for (int i = 0; i < 4; ++i) next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float u() { return (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float bi() { return u() * 2.0f - 1.0f; }                       // -1..1
};

// Hauteur d'un grain : applique PITCH + PJIT + OCT + FIFTH + QUANT (en demi-tons).
inline float grain_semitones(const Params& p, Rng& r, float extraSemis = 0.0f) {
    float st = semis(p.v[P_PITCH]) + extraSemis + r.bi() * 12.0f * prob(p.v[P_PJIT]);
    if (r.u() < prob(p.v[P_OCT]))   st += (r.u() < 0.5f ? 12.0f : -12.0f);
    if (r.u() < prob(p.v[P_FIFTH])) st += 7.0f;
    const int q = (p.v[P_QUANT] * 8) / 256;
    if (q > 0) {
        static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10},
                         PEN[5] = {0, 2, 4, 7, 9}, WHL[6] = {0, 2, 4, 6, 8, 10}, FIF[3] = {0, 7, 12};
        const int* set = nullptr; int n = 0;
        switch (q) { case 2: set = MAJ; n = 7; break; case 3: set = MIN; n = 7; break;
                     case 4: set = PEN; n = 5; break; case 5: set = WHL; n = 6; break;
                     case 7: set = FIF; n = 3; break; default: break; }
        const int   oct  = (int)std::floor(st / 12.0f);
        const float rem  = st - oct * 12.0f;
        if (q == 1) st = std::round(st);
        else if (q == 6) st = std::round(st / 12.0f) * 12.0f;
        else if (set) {
            float best = 1e9f; int bi = 0;
            for (int i = 0; i < n; ++i) { const float d = std::fabs(rem - set[i]); if (d < best) { best = d; bi = i; } }
            st = oct * 12.0f + set[bi];
        }
    }
    return st;
}

}  // namespace grainfx
