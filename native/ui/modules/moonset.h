#pragma once

#include <string>

#include "songcore/model.h"
#include "ui/canvas.h"
#include "ui/cursor.h"
#include "ui/theme.h"

namespace pt::ui {

// The MOONSET editor: the MOONGRANULAR instrument's parameter page. Full-screen (640�480).
struct MoonsetState {
    int instrumentId = 0;
    int cursorRow    = 0;   // line within the block
    int cursorColumn = 0;   // 0 = GRAIN, 1 = ROAM, 2 = MOTION

    // The whole sample's waveform (min/max pairs), for the visualizer above the block.
    std::vector<float> waveformData;
    int sampleRate = 48000;
    int totalFrames = 0;
    // 0..1 while the instrument sounds, -1 when not.
    float playbackPosition = -1.0f;
};

class MoonsetModule {};

// The granular block: helpers used by the page and by the dispatcher.
int  moonset_row_count(int col);
std::string moonset_value(const songcore::Instrument::GranularParams& g, int col, int line,
                          int sliceMethod, int sliceIndex);
CursorContext moonset_cell_context(const songcore::Instrument::GranularParams& g, int col, int line,
                                   int sliceMethod);
bool moonset_apply(songcore::Instrument::GranularParams& g, int col, int line, int value,
                   int sliceMethod);
void draw_moonset_granular(Canvas& c, int x, int gy,
                           const songcore::Instrument::GranularParams& g,
                           int cursorLine, int cursorCol, const Theme& t,
                           int sliceMethod, int sliceIndex);

}  // namespace pt::ui
