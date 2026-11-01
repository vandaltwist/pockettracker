// The held-button chords: A, B and Y with the D-pad, delete, MUTE and SOLO. The screen's handler is
// asked first (ui/dispatch/screens/); what is here runs when it passes the gesture on.

#include "ui/dispatch/dispatch_common.h"

#include "ui/mixer_cell_layout.h"
#include "ui/navigation.h"
#include "ui/song_pointer.h"

#include <algorithm>

namespace pt::ui {

// ─── A + D-pad ───────────────────────────────────────────────────────────────────────────────────

// ⚠️ `pt::ui::on_a_b` must stay qualified: unqualified, a member's lookup finds the member. The others
// match. The horizontal axis is the small step, the vertical the large one.

void InputDispatcher::on_a_up() {
    if (route(Gesture::A_UP)) return;
    selection_or_single(pt::ui::increment_fast);
}

void InputDispatcher::on_a_down() {
    if (route(Gesture::A_DOWN)) return;
    selection_or_single(pt::ui::decrement_fast);
}

void InputDispatcher::on_a_left() {
    if (route(Gesture::A_LEFT)) return;
    selection_or_single(pt::ui::decrement);
}

void InputDispatcher::on_a_right() {
    if (route(Gesture::A_RIGHT)) return;
    selection_or_single(pt::ui::increment);
}

void InputDispatcher::on_a_released() {
    // Nothing else starts a preview while A is down, so this silences the one A began.
    if (heldNotePreview_) {
        heldNotePreview_ = false;
        host_.stop_preview(/*cut=*/true);
    }
    insertHeld_ = false;
    // Only a layer answers the release — the two pickers commit on it.
    route(Gesture::A_RELEASE);
}

void InputDispatcher::on_a_deferred() {
    // The mapper is holding this press; record the playhead now, since it will have moved by release.
    samplePending_.sliceTapPlayhead = on_slice_tap_cell() ? s_.sampleEditor.playbackPosition : -1.0f;
}

// ─── A+B: delete / reset ─────────────────────────────────────────────────────────────────────────

void InputDispatcher::on_a_b() {
    if (route(Gesture::A_B)) return;
    generic_input(pt::ui::on_a_b);
}

// ─── A,A: insert the next UNUSED item — SONG, CHAIN and PHRASE answer it ─────────────────────────

void InputDispatcher::on_a_a() { route(Gesture::A_A); }

// ─── B + D-pad: which item am I looking at? — each screen answers it ─────────────────────────────

void InputDispatcher::on_b_left()  { route(Gesture::B_LEFT); }
void InputDispatcher::on_b_right() { route(Gesture::B_RIGHT); }
void InputDispatcher::on_b_up()    { route(Gesture::B_UP); }
void InputDispatcher::on_b_down()  { route(Gesture::B_DOWN); }

// ─── OPTION + SHIFT / OPTION + PLAY: MUTE and SOLO ───────────────────────────────────────────────
//
// The chord acts on the press and stays armed: releasing OPTION first keeps what it did, releasing
// SHIFT or PLAY first puts the mix back. That is the M8 rule, and it is why OPTION is the arm.

void InputDispatcher::mute_solo_targets(int (&out)[8], int& count) const {
    count = 0;
    switch (s_.currentScreen) {
        case ScreenType::SONG: {
            // A selection makes the chord act on every channel it covers.
            if (s_.selection.active) {
                const SelectionBounds b = s_.selection.bounds();
                for (int col = b.topLeftColumn; col <= b.bottomRightColumn; ++col)
                    if (col >= 1 && col <= 8) out[count++] = col - 1;
                return;
            }
            if (s_.cursorColumn >= 1 && s_.cursorColumn <= 8) out[count++] = s_.cursorColumn - 1;
            return;
        }
        case ScreenType::MIXER:
            // ⚠️ The row is part of the address (row 1 holds the REV and DEL returns); the master
            // has no mute.
            switch (mixer_cell_at(s_.mixerMasterRow, s_.mixerCursorColumn)) {
                case MixerCell::TRACK_VOL: out[count++] = s_.mixerCursorColumn;      break;
                case MixerCell::REV_WET:   out[count++] = songcore::MIX_CH_REVERB;   break;
                case MixerCell::DLY_WET:   out[count++] = songcore::MIX_CH_DELAY;    break;
                case MixerCell::MASTER_VOL:
                case MixerCell::MASTER_EQ:
                case MixerCell::MASTER_FX:
                case MixerCell::LIMITER:
                case MixerCell::NONE:      break;
            }
            return;
        default:
            return;   // every other screen: the chord is the consumed no-op it has always been
    }
}

void InputDispatcher::toggle_mute_solo(bool solo) {
    int targets[8];
    int count = 0;
    mute_solo_targets(targets, count);
    if (count == 0) return;

    Project& p = host_.edit_project();

    // ⚠️ Snapshot on the chord's FIRST toggle only, so a revert undoes the whole chord.
    if (!mixSnapshot_.live) {
        for (int ch = 0; ch < MIX_CHANNELS; ++ch) {
            const songcore::MixChannelFlags f = songcore::mix_channel_flags(p, ch);
            if (!f.mute) continue;
            mixSnapshot_.mute[ch] = *f.mute;
            mixSnapshot_.solo[ch] = *f.solo;
        }
        mixSnapshot_.live = true;
    }

    for (int i = 0; i < count; ++i) {
        const songcore::MixChannelFlags f = songcore::mix_channel_flags(p, targets[i]);
        if (!f.mute) continue;   // a channel the project does not have
        bool& flag = solo ? *f.solo : *f.mute;
        flag = !flag;
    }

    s_.lastClearable = AppState::Clearable::MUTE;

    // ⚠️ Not an edit: no dirty, no autosave. All eight tracks are pushed — a solo changes the others.
    host_.push_globals();
}

void InputDispatcher::restore_full_playback() {
    Project& p = host_.edit_project();
    for (songcore::Track& t : p.tracks) { t.mute = false; t.solo = false; }
    p.reverbMute = p.reverbSolo = p.delayMute = p.delaySolo = false;
    s_.lastClearable = AppState::Clearable::NONE;
    host_.push_globals();
}

// Is OPTION+SHIFT / OPTION+PLAY a MUTE/SOLO here?
bool InputDispatcher::mute_solo_chord_live() const {
    if (top_layer()) return false;   // a modal owns the buttons while it is up
    return s_.currentScreen == ScreenType::SONG || s_.currentScreen == ScreenType::MIXER ||
           s_.currentScreen == ScreenType::EFFECTS;
}

void InputDispatcher::on_mute_chord(bool solo) {
    if (!mute_solo_chord_live()) return;
    toggle_mute_solo(solo);
    muteChordArmed_ = mixSnapshot_.live;
}

void InputDispatcher::on_mute_clear() {
    restore_full_playback();
    muteChordArmed_  = false;
    mixSnapshot_.live = false;
}

void InputDispatcher::on_mute_chord_commit() {
    // OPTION came up first: what the chord did stands.
    muteChordArmed_   = false;
    mixSnapshot_.live = false;
}

void InputDispatcher::on_mute_chord_revert() {
    if (!muteChordArmed_) return;
    muteChordArmed_ = false;
    if (!mixSnapshot_.live) return;
    Project& p = host_.edit_project();
    for (int ch = 0; ch < MIX_CHANNELS; ++ch) {
        const songcore::MixChannelFlags f = songcore::mix_channel_flags(p, ch);
        if (!f.mute) continue;
        *f.mute = mixSnapshot_.mute[ch];
        *f.solo = mixSnapshot_.solo[ch];
    }
    mixSnapshot_.live = false;
    host_.push_globals();
}

}  // namespace pt::ui
