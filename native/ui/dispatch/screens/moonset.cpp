// MOONSET: the granular instrument's own editor — its buttons, opening and closing. The parameters
// and the drawing live in ui/modules/moonset.h (draw_moonset_granular); this file is the dispatcher's half.

#include "ui/dispatch/dispatch_common.h"

namespace pt::ui {

GestureResult InputDispatcher::moonset_screen(Gesture g) {
    switch (g) {
        case Gesture::B:
            close_moonset();
            return GestureResult::TAKEN;

        // X is the transport: it auditions the whole instrument with the parameters as they stand.
        case Gesture::X:
            host_.set_preview_track(audition_track());
            host_.preview_instrument(s_.currentInstrument);
            return GestureResult::TAKEN;

        case Gesture::STOP_PREVIEW:
            host_.stop_preview();
            return GestureResult::TAKEN;

        default:
            return GestureResult::PASS;
    }
}

CursorContext InputDispatcher::moonset_context() const {
    const Instrument& ins = s_.project->instruments[static_cast<size_t>(s_.currentInstrument)];
    return moonset_cell_context(ins.granular, s_.moonset.cursorColumn, s_.moonset.cursorRow, /*sliceMethod=*/0);
}

bool InputDispatcher::moonset_edit(const InputAction& action) {
    Project&    p   = host_.edit_project();
    Instrument& ins = p.instruments[static_cast<size_t>(s_.currentInstrument)];

    if (action.type != ActionType::SET_VALUE) return false;

    const int col = s_.moonset.cursorColumn;
    const int row = s_.moonset.cursorRow;
    const std::string before = moonset_value(ins.granular, col, row, /*sliceMethod=*/0, /*sliceIndex=*/0);

    const bool ok = moonset_apply(ins.granular, col, row, action.value, /*sliceMethod=*/0);

    const std::string after = moonset_value(ins.granular, col, row, /*sliceMethod=*/0, /*sliceIndex=*/0);
    if (ok && before != after) {
        host_.push_instrument(s_.currentInstrument, /*refreshSounding=*/true);
        return true;
    }
    return false;
}

// ─── Opening and closing ─────────────────────────────────────────────────────────────────────────
void InputDispatcher::open_moonset() {
    const Instrument& ins = s_.project->instruments[static_cast<size_t>(s_.currentInstrument)];
    if (ins.instrumentType != songcore::InstrumentType::GRANULAR) return;

    s_.moonset              = MoonsetState{};
    s_.moonset.instrumentId = s_.currentInstrument;

    s_.previousScreen = s_.currentScreen;
    s_.currentScreen  = ScreenType::MOONSET;
    init_moonset_state();
}

void InputDispatcher::init_moonset_state() {
    s_.moonset.cursorRow    = 0;
    s_.moonset.cursorColumn = 0;
    s_.moonset.playbackPosition = -1.0f;
    s_.moonset.waveformData.clear();
}

void InputDispatcher::close_moonset() {
    host_.stop_preview();
    s_.currentScreen = s_.previousScreen;
}

}  // namespace pt::ui