#pragma once

// The combo matrix: a button event plus the modifiers held at that instant → one named handler.
// A template so a test can drive it with a recording stub; in the app `Dispatcher` is `InputDispatcher`.
//
// ── The M8 layout ────────────────────────────────────────────────────────────────────────────────
// Four buttons, printed OPTION / EDIT / SHIFT / PLAY, arriving as A / B / Y / X:
//
//     OPTION = A = M     CANCEL / EXIT        → the dispatcher's historical `b` handlers
//     EDIT   = B = J     CONFIRM / EDIT       → the dispatcher's historical `a` handlers
//     SHIFT  = Y = N     the modifier         → `y`
//     PLAY   = X = K     the transport        → `x`
//
// ⚠️ Every `a` and `b` below is a ROLE, not a physical button: `role_of()` trades A and B on the way in
// and `m.a`/`m.b` with it. The whole screen layer (ui/dispatch/) therefore stays on one convention —
// `Gesture::A` is EDIT (validate, edit, insert) and `Gesture::B` is OPTION (cancel, exit, which item) —
// and a screen never has to know which physical button sent it.
//
// ⚠️ The mods INCLUDE the button being pressed (shell/sdl/sdl-input.cpp sets the flag before queueing),
// so a plain press tests the OTHER roles only: `!m.a && !m.y && !m.x` for OPTION, never `!m.b`.

#include "ui/buttons.h"

#include <cstdint>

namespace pt::ui {

// The mapper's memory between events.
struct MapperState {
    uint64_t lastAPress = 0;   // the EDIT,EDIT window
};

// ⚠️ The order of the checks is the specification: most specific first, each arm returns. EDIT+SHIFT+
// OPTION before SHIFT+EDIT and SHIFT+OPTION, EDIT+OPTION before EDIT, EDIT+DPAD before EDIT.
// ⚠️ Modifiers come from the event, never a poll: a frame's events arrive at once, and a poll would
// see the frame's end (OPTION-then-EDIT in one frame would cancel on the OPTION press).
template <class Dispatcher>
void handle_button(const ButtonEvent& e, Dispatcher& d, MapperState& ms, uint64_t now) {
    // Quel role logique le bouton physique e.button joue-t-il, selon le mappage reglage (settings) ?
    // Seuls les boutons faces A/B/X/Y sont remappables ; un D-pad reste tel quel.
    Role roleOfPhysical[4];
    for (int r = 0; r < 4; ++r)
        roleOfPhysical[d.physical_for_role(static_cast<Role>(r))] = static_cast<Role>(r);
    const int face = face_index(e.button);
    const Button button = (face >= 0)
        ? role_button(roleOfPhysical[static_cast<size_t>(face)])
        : e.button;

    // Les modificateurs se lisent sur le bouton physique de chaque role (et non sur le bouton presse).
    auto mod_held = [](const ButtonMods& mods, int physical) {
        switch (physical) {
            case 0: return mods.a;
            case 1: return mods.b;
            case 2: return mods.x;
            default: return mods.y;
        }
    };
    ButtonMods m{
        mod_held(e.mods, d.physical_for_role(Role::EDIT)),
        mod_held(e.mods, d.physical_for_role(Role::OPTION)),
        mod_held(e.mods, d.physical_for_role(Role::PLAY)),
        mod_held(e.mods, d.physical_for_role(Role::SHIFT)),
    };

    // EDIT's state for what is drawn while it is held. Above every early return too.
    if (button == Button::A) d.on_a_held(e.action == ButtonAction::PRESSED);

    // ── Release ──────────────────────────────────────────────────────────────────────────────────
    if (e.action != ButtonAction::PRESSED) {
        // A MUTE/SOLO chord ends on a release: OPTION up first keeps what it did, SHIFT or PLAY up
        // first puts the mix back.
        if (d.mute_chord_armed()) {
            if (button == Button::B) {
                d.on_mute_chord_commit();
                return;
            }
            if (button == Button::Y || button == Button::X) {
                d.on_mute_chord_revert();
                return;
            }
        }
        if (button == Button::A) {
            if (d.a_pressed_alone()) {
                d.set_a_pressed_alone(false);
                d.on_button_a();
            }
            d.on_a_released();   // the FX helper commits here
        }
        if (button == Button::B) {
            // OPTION deferred to release (EQ editor): fire it now.
            if (d.defer_b_to_release()) d.on_button_b();
        }
        return;
    }

    // The full help overlay: any press closes it and is consumed — it covers the editor, so a press
    // falling through would edit a cell nobody can see. An audition is still silenced.
    if (d.help_full_open()) {
        d.on_help_dismiss();
        if (!m.a) d.on_stop_preview();
        return;
    }

    // ── SHIFT alone: the cell help (the old SELECT) ───────────────────────────────────────────────
    // ⚠️ Before `on_help_dismiss()`: that clears the panel on every press, so a toggle after it could
    // only ever open. SHIFT does nothing else alone — its jobs are all with another button.
    if (button == Button::Y && !m.a && !m.b && !m.x) {
        d.on_help_toggle();
        return;
    }

    // Any press puts the panel away and falls through — it swallows nothing.
    d.on_help_dismiss();

    // Any plain press silences an audition; EDIT-combos (edits stay audible) do not.
    if (!m.a) d.on_stop_preview();

    // ── EDIT + SHIFT + OPTION: clone — before everything else ─────────────────────────────────────
    if (m.a && m.y && m.b && button == Button::A) {
        d.on_a_y_b();
        return;
    }

    // ── SHIFT + EDIT: paste (interpolate in a selection) ──────────────────────────────────────────
    if (m.y && m.a && button == Button::A) {
        d.on_y_a();
        return;
    }

    // ── SHIFT + OPTION: selection mode ────────────────────────────────────────────────────────────
    if (m.y && m.b && button == Button::B) {
        d.on_y_b();
        return;
    }

    // ── OPTION + SHIFT: MUTE (OPTION was down first — SHIFT first is the selection above) ─────────
    if (button == Button::Y && m.b && !m.a) {
        d.on_mute_chord(/*solo=*/false);
        return;
    }

    // ── OPTION + SHIFT + PLAY: every track audible again ─────────────────────────────────────────
    if (button == Button::X && m.b && m.y && !m.a) {
        d.on_mute_clear();
        return;
    }

    // ── OPTION + PLAY: SOLO ──────────────────────────────────────────────────────────────────────
    if (button == Button::X && m.b && !m.y && !m.a) {
        d.on_mute_chord(/*solo=*/true);
        return;
    }

    // ── EDIT + … ──────────────────────────────────────────────────────────────────────────────────
    if (m.a && !m.y) {
        // EDIT+OPTION: delete / cut / reset — whichever of the two came second.
        if (m.b) {
            d.on_a_b();
            return;
        }
        switch (button) {
            case Button::DPAD_UP:    d.on_a_up();    return;   // +16 / +1 octave
            case Button::DPAD_DOWN:  d.on_a_down();  return;   // −16 / −1 octave
            case Button::DPAD_RIGHT: d.on_a_right(); return;   // +1
            case Button::DPAD_LEFT:  d.on_a_left();  return;   // −1
            case Button::X:          d.on_x_a();     return;   // EDIT+PLAY: audition
            default: break;
        }
    }

    // ── OPTION + DPAD: which item / page ─────────────────────────────────────────────────────────
    if (m.b && !m.a && !m.y) {
        switch (button) {
            case Button::DPAD_LEFT:  d.on_b_left();  return;   // prev item / EQ slot −1
            case Button::DPAD_RIGHT: d.on_b_right(); return;   // next item / EQ slot +1
            case Button::DPAD_UP:    d.on_b_up();    return;   // SONG / pool: page up
            case Button::DPAD_DOWN:  d.on_b_down();  return;   // SONG / pool: page down
            default: break;
        }
    }

    // ── SHIFT + DPAD: move between screens / cycle sort / zoom / step section ─────────────────────
    if (m.y && !m.a && !m.b) {
        switch (button) {
            case Button::DPAD_UP:    d.on_y_up();    return;
            case Button::DPAD_DOWN:  d.on_y_down();  return;
            case Button::DPAD_LEFT:  d.on_y_left();  return;
            case Button::DPAD_RIGHT: d.on_y_right(); return;
            default: break;
        }
    }

    // ── EDIT, and the EDIT,EDIT double-tap (300 ms) ───────────────────────────────────────────────
    if (button == Button::A && !m.y && !m.b) {
        if (d.defer_a_to_release()) {
            d.set_a_pressed_alone(true);
            d.on_a_deferred();
            return;
        }
        if (now - ms.lastAPress < 300) {
            ms.lastAPress = 0;   // so a triple-tap is not two double-taps
            d.on_a_a();
        } else {
            ms.lastAPress = now;
            d.on_button_a();
        }
        return;
    }

    // ── PLAY + OPTION: arm delete (file browser) ─────────────────────────────────────────────────
    if (m.x && !m.a && !m.y && button == Button::B) {
        d.on_x_b();
        return;
    }

    // ── OPTION alone: cancel / exit ───────────────────────────────────────────────────────────────
    if (button == Button::B && !m.a && !m.y && !m.x) {
        if (d.defer_b_to_release()) return;
        d.on_button_b();
        return;
    }

    // ── PLAY alone: the transport ────────────────────────────────────────────────────────────────
    if (button == Button::X && !m.a && !m.b && !m.y) {
        d.on_button_x();
        return;
    }

    // ── SHIFT + PLAY: play the song from the cursor ───────────────────────────────────────────────
    if (button == Button::X && m.y && !m.a && !m.b) {
        d.on_y_x();
        return;
    }

    // ── The plain D-pad ──────────────────────────────────────────────────────────────────────────
    if (m.a || m.b || m.x || m.y) return;

    switch (button) {
        case Button::DPAD_UP:    d.on_dpad_up();    break;
        case Button::DPAD_DOWN:  d.on_dpad_down();  break;
        case Button::DPAD_LEFT:  d.on_dpad_left();  break;
        case Button::DPAD_RIGHT: d.on_dpad_right(); break;
        default: break;
    }
}

}  // namespace pt::ui
