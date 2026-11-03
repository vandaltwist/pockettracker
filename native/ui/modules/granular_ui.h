#pragma once

#include <cstdint>

// ─── TIME divisions (beats = durée en noires) ──────────────────────────────
struct Div { const char* name; float beats; };   // beats = durée en noires
inline constexpr Div DIVS[] = {
    {"4 BAR", 16.0000f},
    {"2 BAR",  8.0000f},
    {"1 BAR",  4.0000f},
    {"1/2",    2.0000f},
    {"1/4",    1.0000f},
    {"1/8",    0.5000f},
    {"1/16",   0.2500f},
    {"1/32",   0.1250f},
    {"1/64",   0.0625f},
    {"1/4T",   0.6667f},
    {"1/8T",   0.3333f},
    {"1/16T",  0.1667f},
    {"1/4D",   1.5000f},
    {"1/8D",   0.7500f},
    {"1/16D",  0.3750f},
};
inline constexpr int DIV_COUNT = 15;
// période (s) = beats * 60 / bpm ; densité = 1/période ; vitesse ROAM = 1/période

// ─── GRow : définition d'un paramètre granulaire (UI-only) ────────────────
enum class GKind { BOOL, INT, DEC, ENUM };
struct GRow { const char* label; GKind kind; int min, max, step, big, scale; const char* unit; };

inline constexpr GRow GRAIN[] = {
    {"STATE",   GKind::BOOL, 0, 1,   1, 1, 1, ""},
    {"POSITION",GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"SIZE",    GKind::INT,  10, 4000, 10, 100, 1, "MS"},
    {"DENSITY", GKind::DEC,  1, 1000, 5, 50, 10, "HZ"},
    {"SPRAY",   GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"PITCH",   GKind::INT,  -48, 48, 1, 12, 1, "ST"},
    {"TIME",    GKind::ENUM, 0, 1,   1, 1, 1, ""},
    {"ATTACK",  GKind::INT,  0, 100, 1, 10, 1, "MS"},
    {"DECAY",   GKind::INT,  0, 100, 1, 10, 1, "MS"},
    {"PAN",     GKind::INT,  -50, 50, 1, 10, 1, "%"},
    {"JITTER",  GKind::INT,  0, 100, 1, 10, 1, "%"},
};
inline constexpr int GRAIN_COUNT = 11;

inline constexpr GRow ROAM[] = {
    {"STATE",    GKind::BOOL, 0, 1,   1, 1, 1, ""},
    {"MODE",     GKind::ENUM, 0, 3,   1, 1, 1, ""},
    {"RANGE",    GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"WANDER",   GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"SPEED",    GKind::DEC,  0, 400, 5, 25, 100, "X"},
    {"SMOOTH",   GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"DIR",      GKind::ENUM, 0, 2,   1, 1, 1, ""},
    {"CURVE",    GKind::ENUM, 0, 2,   1, 1, 1, ""},
    {"BPM",      GKind::INT,  20, 300, 1, 10, 1, ""},
    {"DRIFT",    GKind::ENUM, 0, 2,   1, 1, 1, ""},
    {"STEP_RATE",GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"INERTIA",  GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"TARGET",   GKind::INT,  0, 100, 1, 10, 1, "%"},
};
inline constexpr int ROAM_COUNT = 13;

inline constexpr GRow MOTION[] = {
    {"STATE",       GKind::BOOL, 0, 1,   1, 1, 1, ""},
    {"JUMP",        GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"HOLD",        GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"REV",         GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"ELASTIC",     GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"CHAOS",       GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"PROBABILITY", GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"LFO_DEPTH",   GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"STEP_GLIDE",  GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"TRIGGER",     GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"SPIN",        GKind::INT,  0, 100, 1, 10, 1, "%"},
    {"FACTOR",      GKind::INT,  0, 100, 1, 10, 1, ""},
};
inline constexpr int MOTION_COUNT = 11;

// ─── GranularUi : état UI-only des paramètres granulaires ─────────────────
// Pas encore lié à Instrument::GranularParams pour les champs manquants (v1).
struct GranularUi {
    int grain_state    = 1;
    int grain_position = 50;
    int grain_size     = 1000;
    int grain_density  = 500;   // /10
    int grain_spray    = 14;
    int grain_pitch    = -24;
    int grain_time     = 0;     // 0 = HZ, 1 = BPM
    int grain_attack   = 10;
    int grain_decay    = 50;
    int grain_pan      = 0;
    int grain_jitter   = 0;

    int roam_state     = 1;
    int roam_mode      = 0;     // BOUNCE | LOOP | DRIFT | RANDOM
    int roam_range     = 15;
    int roam_wander    = 0;
    int roam_speed     = 30;    // /100
    int roam_smooth    = 92;
    int roam_dir       = 0;     // FWD | REV | RND
    int roam_curve     = 0;     // LIN | SINE | STEP
    int roam_bpm       = 120;
    int roam_drift     = 0;     // BOUNCE | LOOP | DRIFT
    int roam_step_rate = 50;
    int roam_inertia   = 50;
    int roam_target    = 50;

    int motion_state   = 1;
    int motion_jump    = 100;
    int motion_hold    = 10;
    int motion_rev     = 48;
    int motion_elastic = 0;
    int motion_chaos   = 0;
    int motion_probability = 50;
    int motion_lfo_depth   = 0;
    int motion_step_glide  = 0;
    int motion_trigger_rate= 50;
    int motion_spin_factor = 0;
};
