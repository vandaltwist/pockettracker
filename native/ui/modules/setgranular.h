// setgranular.h -- l'ecran SETGRANULAR : une boite modale sur l'editeur de sample, construite comme la
// boite TSTRETCH (meme look, memes touches). Les lignes viennent de la table grainfx::PARAM_INFO :
// ajouter un effet = une ligne dans grain-params.h, rien a changer ici.
//
//   HAUT/BAS : ligne      A+GAUCHE/DROITE : -1/+1      A+HAUT/BAS : +16/-16      A+B : valeur par defaut
//   PREVIEW / APPLY : GAUCHE/DROITE entre les deux.      B : ferme (les reglages restent)
//   Un "*" apres le nom = ce reglage est AUSSI lu en temps reel par l'instrument GRAINSYNTH.
#pragma once

#include <algorithm>
#include <string>

#include "granular/grain-params.h"
#include "ui/canvas.h"
#include "ui/cursor.h"
#include "ui/helpers.h"
#include "ui/theme.h"

namespace pt::ui {

struct SetGranularState {
    bool            open   = false;
    int             cursor = 0;          // 0..P_COUNT-1 = un reglage ; P_COUNT = PREVIEW ; P_COUNT+1 = APPLY
    grainfx::Params p;                   // vit avec la session (pas dans le document) ; copie dans Instrument au SAVE de l'instrument
};

inline constexpr int SETGRAN_PREVIEW = grainfx::P_COUNT;
inline constexpr int SETGRAN_APPLY   = grainfx::P_COUNT + 1;

inline bool setgranular_on_buttons(int cur) { return cur >= SETGRAN_PREVIEW; }

inline int setgranular_step(int from, int dRow, int dCol) {
    if (dCol != 0 && setgranular_on_buttons(from)) return dCol > 0 ? SETGRAN_APPLY : SETGRAN_PREVIEW;
    if (dRow == 0) return from;
    if (setgranular_on_buttons(from)) return dRow < 0 ? grainfx::P_COUNT - 1 : from;
    return std::clamp(from + dRow, 0, SETGRAN_PREVIEW);
}

inline CursorContext setgranular_cursor_context(const SetGranularState& s) {
    if (setgranular_on_buttons(s.cursor)) return cc::none();
    const int i = s.cursor;
    CursorContext c = cc::hex_byte(s.p.v[i], 0, 255, -1, false, false, false, grainfx::PARAM_INFO[i].def);
    switch (i) {   // les listes avancent d'une entree par pas
        case grainfx::P_SHAPE:  c.smallStep = 43; c.largeStep = 43; break;
        case grainfx::P_QUANT:  c.smallStep = 32; c.largeStep = 32; break;
        case grainfx::P_FREEZE: c.smallStep = 128; c.largeStep = 128; break;
        default: break;
    }
    return c;
}

inline void setgranular_handle_input(SetGranularState& s, const InputAction& a) {
    if (a.type != ActionType::SET_VALUE || setgranular_on_buttons(s.cursor)) return;
    s.p.v[s.cursor] = (uint8_t)std::clamp(a.value, 0, 255);
}

// ---- Dessin ---------------------------------------------------------------------------------------
namespace setgran_detail {
constexpr int VISIBLE = 7;                                  // lignes de reglages visibles
constexpr int BOX_W   = 440;
constexpr int BOX_X   = (DESIGN_W - BOX_W) / 2;
constexpr int BOX_Y   = 85 + 155 + 8;                       // sous la forme d'onde (WAVEFORM_Y + WAVEFORM_H + 8)
constexpr int PAD = 8, GAP = 8;
constexpr int BOX_H   = ROW_HEIGHT + GAP + (VISIBLE + 1) * ROW_HEIGHT + 2 * PAD;
static_assert(BOX_Y + BOX_H + MODAL_BORDER - 1 <= DESIGN_H, "la boite SETGRANULAR sort de l'ecran");
}  // namespace setgran_detail

inline void draw_setgranular(Canvas& c, const SetGranularState& s, const Theme& t) {
    using namespace setgran_detail;
    if (!s.open) return;
    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, BOX_X, BOX_Y, BOX_W, BOX_H, t);

    const int labelX = BOX_X + 14, valueX = labelX + 8 * CHAR_W, barX = valueX + 5 * CHAR_W, barW = 80;
    int ty = BOX_Y + PAD + TEXT_PADDING;

    const std::string title = "SETGRANULAR";
    c.draw_text(title, BOX_X + (BOX_W - (int)title.size() * CHAR_W) / 2, ty, t.textTitle, CHAR_SPACING, FONT_SCALE);
    // position dans la liste, a droite du titre
    const int shown = std::min(s.cursor, grainfx::P_COUNT - 1) + 1;
    const std::string count = std::to_string(shown) + "/" + std::to_string(grainfx::P_COUNT);
    c.draw_text(count, BOX_X + BOX_W - 14 - Canvas::text_width(count, CHAR_SPACING, FONT_SCALE), ty,
                t.textParam, CHAR_SPACING, FONT_SCALE);
    ty += ROW_HEIGHT + GAP;

    const int cur   = std::min(s.cursor, grainfx::P_COUNT - 1);
    const int first = std::clamp(cur - VISIBLE / 2, 0, grainfx::P_COUNT - VISIBLE);

    for (int r = 0; r < VISIBLE; ++r, ty += ROW_HEIGHT) {
        const int i = first + r;
        const grainfx::ParamInfo& info = grainfx::PARAM_INFO[i];
        const bool on = (s.cursor == i);
        std::string lab = info.label;
        if (info.live) lab += "*";
        c.draw_text(lab, labelX, ty, on ? cursor_mark_ink(t) : t.textParam, CHAR_SPACING, FONT_SCALE);
        draw_cursor_cell(c, grainfx::format(i, s.p.v[i]), valueX, ty, on, t.textValue, t);
        // barre de valeur + nom du groupe
        c.fill_rect(barX, ty + 5, barW, 6, t.eqFill);
        c.fill_rect(barX, ty + 5, (barW * s.p.v[i]) / 255, 6, t.vizWave);
        c.draw_text(info.group, barX + barW + 10, ty, t.textParam, CHAR_SPACING, FONT_SCALE - 1);
    }

    draw_cursor_cell(c, "PREVIEW", labelX, ty, s.cursor == SETGRAN_PREVIEW, t.textValue, t);
    draw_cursor_cell(c, "APPLY",   valueX, ty, s.cursor == SETGRAN_APPLY,   t.textValue, t);
}

}  // namespace pt::ui
