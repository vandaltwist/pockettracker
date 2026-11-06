// The plain buttons — X, Y, A, B — and their combos.

#include "ui/dispatch/dispatch_common.h"

#include "songcore/traversal.h"
#include "ui/navigation.h"
#include "ui/song_pointer.h"

namespace pt::ui {

// ─── The plain buttons ───────────────────────────────────────────────────────────────────────────

// X is the transport: play/stop, what it plays depends on the screen.
void InputDispatcher::on_button_x() {
    if (route(Gesture::X)) return;
    if (host_.is_playing()) {
        host_.stop();
        return;
    }
    switch (s_.currentScreen) {
        case ScreenType::SONG:  host_.play_song(s_.cursorRow); break;

        case ScreenType::CHAIN:
            host_.play_chain(s_.currentChain,
                             songcore::track_of_chain(*s_.project, s_.currentChain,
                                                      remembered_song_track()));
            break;

        case ScreenType::MIXER:
        case ScreenType::EFFECTS:
        case ScreenType::PROJECT:
        case ScreenType::SETTINGS:
        case ScreenType::MIDI:
        case ScreenType::MIDI_MAP: host_.play_song(0); break;

        default:
            host_.play_phrase(s_.currentPhrase,
                              songcore::track_of_phrase(*s_.project, s_.currentPhrase, s_.currentChain,
                                                        remembered_song_track()));
            break;
    }
}

// Y is SHIFT: alone it is the cell help (the old SELECT); with a direction it changes screen.
void InputDispatcher::on_button_y() { on_help_toggle(); }

// SHIFT + PLAY: the whole song from the cursor's row — all tracks, not just the edited one. On SONG
// that is the live cursor; on the other screens it is the song position they were last pointing at.
void InputDispatcher::on_y_x() {
    if (route(Gesture::Y_X)) return;
    if (top_layer()) return;   // a dialog or editor owns the transport
    switch (s_.currentScreen) {
        case ScreenType::FILE_BROWSER:
        case ScreenType::SAMPLE_EDITOR:
            return;   // no song position to play from
        default:
            host_.play_song(pointer_song_row(s_));
            return;
    }
}

// A and B are the screen's: every screen's handler answers them (ui/dispatch/screens/).
void InputDispatcher::on_button_a() { route(Gesture::A); }
void InputDispatcher::on_button_b() { route(Gesture::B); }

void InputDispatcher::on_help_dismiss() {
    s_.helpOpen = false;
    s_.helpFull = false;
}

void InputDispatcher::on_stop_preview() { route(Gesture::STOP_PREVIEW); }

// ── Y + A: SHIFT + OPTION → selection mode ─────────────────────────────────────────────────────

void InputDispatcher::on_y_a() { route(Gesture::Y_A); }

// ── Y + B: SHIFT + EDIT → selection mode ───────────────────────────────────────────────────────

void InputDispatcher::on_y_b() { route(Gesture::Y_B); }

// ── Y + DPAD: move between screens / cycle sort / zoom / step section ──────────────────────────

void InputDispatcher::on_y_up() {
    if (route(Gesture::Y_UP)) return;
    go_to_screen(s_, navigate_up(nav_state_of(s_)));
    s_.selection.exit();
}

void InputDispatcher::on_y_down() {
    if (route(Gesture::Y_DOWN)) return;
    go_to_screen(s_, navigate_down(nav_state_of(s_)));
    s_.selection.exit();
}

void InputDispatcher::on_y_left() {
    if (route(Gesture::Y_LEFT)) return;
    const NavResult r = navigate_left(nav_state_of(s_));
    if (r.screen != s_.currentScreen) sync_last_edited_on_screen_switch(s_.currentScreen, r.screen);
    go_to_screen(s_, r);
    s_.selection.exit();
}

void InputDispatcher::on_y_right() {
    if (route(Gesture::Y_RIGHT)) return;

    // On PHRASE, the page change lands on the instrument named by the current row's instrument cell,
    // whatever column the cursor is on: the whole note/row shares one instrument.
    if (s_.currentScreen == ScreenType::PHRASE) {
        const Project& p = *s_.project;
        if (s_.currentPhrase >= 0 && s_.currentPhrase < static_cast<int>(p.phrases.size())) {
            const Phrase& ph = p.phrases[static_cast<size_t>(s_.currentPhrase)];
            if (s_.cursorRow >= 0 && s_.cursorRow < static_cast<int>(ph.steps.size())) {
                const int last = static_cast<int>(p.instruments.size()) - 1;
                const int inst = std::clamp(ph.steps[static_cast<size_t>(s_.cursorRow)].instrument, 0,
                                            std::max(0, last));
                s_.currentInstrument    = inst;
                s_.lastEditedInstrument = inst;
            }
        }
    }

    const NavResult r = navigate_right(nav_state_of(s_));
    if (!song_relative_entry_allowed(s_, r.screen)) return;
    if (r.screen != s_.currentScreen) sync_last_edited_on_screen_switch(s_.currentScreen, r.screen);
    go_to_screen(s_, r);
    s_.selection.exit();
}

// ── X + A: audition the instrument on the instrument screens and TABLE ─────────────────────────

void InputDispatcher::on_x_a() { route(Gesture::X_A); }

// ── X + B: arm delete in the file browser ──────────────────────────────────────────────────────

void InputDispatcher::on_x_b() { route(Gesture::X_B); }

// ── A + Y + B: deep-clone the chain/phrase under the cursor into free slots ────────────────────

void InputDispatcher::on_a_y_b() {
    if (route(Gesture::A_Y_B)) return;
    s_.selection.exit();
}

}  // namespace pt::ui
