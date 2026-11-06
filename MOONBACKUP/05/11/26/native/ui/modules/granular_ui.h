#pragma once

// MOONSET's TIME divisions: the note values DENSITY and SPEED become while GRAIN TIME = BPM.
// `beats` is the length of one step in quarter notes (4 BAR = 16 beats).
//
// ⚠️ The model stores a DIVISION INDEX (grain.density_div / roam.speed_div), not a number: reordering
// this table changes what a saved project plays.

namespace pt::ui {

struct GranDiv {
    const char* name;
    float       beats;
};

inline constexpr GranDiv GRAN_DIVS[] = {
    {"4 BAR",  16.0000f},
    {"2 BAR",   8.0000f},
    {"1 BAR",   4.0000f},
    {"1/2",     2.0000f},
    {"1/4",     1.0000f},
    {"1/8",     0.5000f},
    {"1/16",    0.2500f},
    {"1/32",    0.1250f},
    {"1/64",    0.0625f},
    {"1/4T",    0.6667f},
    {"1/8T",    0.3333f},
    {"1/16T",   0.1667f},
    {"1/4D",    1.5000f},
    {"1/8D",    0.7500f},
    {"1/16D",   0.3750f},
};

inline constexpr int GRAN_DIV_COUNT = 15;

}  // namespace pt::ui
