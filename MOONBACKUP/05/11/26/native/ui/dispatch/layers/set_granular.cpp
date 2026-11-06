// La boite SETGRANULAR : les reglages granulaires de l'editeur de sample, levee par A sur la cellule
// valeur de la ligne FX quand le type est GRAIN. Copie conforme de stretch_editor.cpp (meme contrat) :
// elle prend les boutons tant qu'elle est ouverte, mais laisse passer Y (l'aide).

#include "ui/dispatch/dispatch_common.h"
#include "ui/modules/setgranular.h"

namespace pt::ui {

GestureResult InputDispatcher::set_granular_layer(Gesture g) {
    SampleEditorState& se = s_.sampleEditor;
    SetGranularState&  sg = se.setGranular;
    switch (g) {
        case Gesture::DPAD_UP:    sg.cursor = setgranular_step(sg.cursor, -1, 0); break;
        case Gesture::DPAD_DOWN:  sg.cursor = setgranular_step(sg.cursor, +1, 0); break;
        case Gesture::DPAD_LEFT:  sg.cursor = setgranular_step(sg.cursor, 0, -1); break;
        case Gesture::DPAD_RIGHT: sg.cursor = setgranular_step(sg.cursor, 0, +1); break;

        case Gesture::A_UP:    set_granular_edit(pt::ui::increment_fast); break;
        case Gesture::A_DOWN:  set_granular_edit(pt::ui::decrement_fast); break;
        case Gesture::A_LEFT:  set_granular_edit(pt::ui::decrement); break;
        case Gesture::A_RIGHT: set_granular_edit(pt::ui::increment); break;
        case Gesture::A_B:     set_granular_edit(pt::ui::on_a_b); break;

        case Gesture::A:
            if (sg.cursor == SETGRAN_PREVIEW) {
                host_.set_grain_fx(sg.p);
                sample_editor_audition();
            } else if (sg.cursor == SETGRAN_APPLY) {
                sg.open = false;
                // Meme suite que apply_sample_sync() / la ligne FX : audio propre, sauvegarde pour UNDO,
                // effet, rafraichissement.
                host_.restore_fx_preview_backup();
                host_.backup_sample(se.instrumentId);
                host_.set_grain_fx(sg.p);
                host_.apply_sample_fx(se.instrumentId, SampleEditorModule::FX_GRAIN, 0);
                refresh_sample_view(/*reset_selection=*/false);
                se.isModified = true;
            }
            break;

        case Gesture::X: host_.set_grain_fx(sg.p); sample_editor_audition(); break;   // l'X de l'editeur : ecoute

        // Les reglages restent pour APPLY sur la ligne FX.
        case Gesture::B: sg.open = false; break;

        case Gesture::Y: return GestureResult::PASS;

        default: break;
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::open_set_granular() {
    s_.sampleEditor.setGranular.open   = true;
    s_.sampleEditor.setGranular.cursor = 0;
    host_.set_grain_fx(s_.sampleEditor.setGranular.p);
}

void InputDispatcher::set_granular_edit(InputAction (*fn)(const CursorContext&)) {
    SampleEditorState& se = s_.sampleEditor;
    setgranular_handle_input(se.setGranular, fn(setgranular_cursor_context(se.setGranular)));
    host_.set_grain_fx(se.setGranular.p);          // le moteur a toujours les reglages affiches

    // Pre-ecoute en direct : si un passage est en cours, le relancer avec les nouveaux reglages.
    if (se.playbackPosition >= 0.0f) {
        host_.stop_preview();
        se.playbackPosition = -1.0f;
        sample_editor_audition();
    }
}

}  // namespace pt::ui
