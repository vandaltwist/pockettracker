// grain_overlay.h -- le visualiseur "courbes de niveau" du REC (RecViz), affiche LEGEREMENT TRANSPARENT
// par-dessus la forme d'onde de l'editeur de sample, pour un instrument GRAINSYNTH.
//
// RecViz N'EST PAS MODIFIE. RecViz::draw est un template sur le type de canvas : on lui passe un
// GhostCanvas qui (1) saute le fond noir et tout ce qui n'est pas une trainee de contour, (2) n'ecrit
// ni texte ni cadre, (3) peint chaque contour en teinte + alpha proportionnel a son niveau de gris.
//
// Alimentation : la voix GRAINSYNTH ecrit son signal dans grainfx::g_vizTap (thread audio, lock-free) ;
// draw_grain_overlay() le vide dans rec_viz().push() (thread UI) a chaque image.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include "granular/grain-viz-tap.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/modules/rec_viz.h"
#include "ui/theme.h"

namespace pt::ui {

// Opacite maximale d'un contour (0..255). 0xA0 ~ 63 % : visible, mais la forme d'onde reste lisible.
inline constexpr uint32_t GRAIN_OVERLAY_ALPHA = 0xA0;

class GhostCanvas {
  public:
    GhostCanvas(Canvas& c, uint32_t maxAlpha, Argb tintRgb) : c_(c), a_(maxAlpha), tint_(tintRgb & 0x00FFFFFFu) {}

    void fill_rect(int x, int y, int w, int h, Argb col) {
        if (h != 1) return;                                   // fond, cadres, pointilles, bouton SET : pas de trait d'1 px de haut
        if (w == 3 && col == 0xFFFFFFFFu) return;             // les graduations blanches des axes
        const uint32_t g = col & 0xFFu;                       // niveau de gris du contour (RecViz : r = g = b)
        const uint32_t a = (g * a_) / 255u;
        if (a == 0) return;
        c_.fill_rect(x, y, w, h, (a << 24) | tint_);          // Canvas melange src-over quand alpha < 255
    }
    void stroke_rect(int, int, int, int, Argb, int = 1) {}
    void draw_text(const std::string&, int, int, Argb, int, int) {}
    static int text_width(const std::string& s, int cs, int fs) { return Canvas::text_width(s, cs, fs); }

  private:
    Canvas&  c_;
    uint32_t a_;
    uint32_t tint_;
};

// x, y, w, h : le rectangle de la forme d'onde (SampleEditorModule::WAVEFORM_W / WAVEFORM_H).
inline void draw_grain_overlay(Canvas& c, int x, int y, int w, int h, const Theme& t) {
    RecViz& v = rec_viz();

    // Le signal de la voix, depuis le dernier dessin. Rien recu = du silence : l'historique glisse et s'eteint.
    int pushed = 0;
    grainfx::g_vizTap.drain([&](const float* l, const float* r, int n) { v.push(l, r, n); pushed += n; });
    if (pushed == 0) {
        static const float zeros[800] = {};
        v.push(zeros, zeros, 800);   // ~1/60 s a 48 kHz
    }
    v.update();

    const int cs = CHAR_SPACING, fs = std::max(1, FONT_SCALE - 1);
    // Meme geometrie que le REC : la zone des courbes est alignee sur la forme d'onde (marge gauche des graduations rognee).
    const int ml = Canvas::text_width("5k", cs, fs) + 9;
    GhostCanvas g(c, GRAIN_OVERLAY_ALPHA, t.textTitle);
    v.draw(g, x - ml, y, w + ml + 6, h, cs, fs);
}

}  // namespace pt::ui
