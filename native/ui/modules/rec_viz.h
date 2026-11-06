// RecViz — visualiseur « courbes de niveau » du REC (port de Spectre_L_R_en_courbes_de_niveau.html).
//
//   thread audio : push(L,R,n) ──► anneau ──► thread UI : update() (1 pas / 50 ms d'audio)
//   FFT 4096 (Blackman, lissage 0.4) → 3 couches de fréquence placées selon la balance L/R (PAN)
//   ou 2 spectres L/R en bandes log (FREQ) → seuil + atténuation des graves → traînée
//   → historique 30 s → flou 3×3 (interne) → contours marching squares (4 seuils) / remplissage.
//   Rendu NET (aucun flou à l'écran) : graves = formes pleines, médiums = traits, aigus = pointillés.
//
// Aucune dépendance. Canvas utilisé via fill_rect / stroke_rect / draw_text / text_width.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/theme.h"

namespace pt::ui {

class RecViz {
  public:
    enum class Axis { Pan, Freq };

    struct Params {
        int   track     = -1;    // -1 master, 0..7 song tracks, 8 preview lane
        Axis  axis      = Axis::Pan;
        bool  stereo    = true;
        float gate      = -72.f;  // seuil de base (dB)
        float timePow   = 2.9f;   // compression du passé
        float bass      = 34.f;   // atténuation des graves (dB)
        float fall      = 0.5f;   // traînée (dB / colonne)
        float spread    = 1.8f;   // PAN : élargit (>1) ou resserre (<1) l'amplitude verticale L/R
        float inputGain = 4.f;    // gain d'entrée (4 micro, 1 fichier)
        bool  autoCal   = false;  // étalonne le seuil sur le silence au début de chaque REC (OFF : le REC démarre tout de suite)
        float micGainDb = 12.f;   // MIC GAIN : gain appliqué au sample enregistré (dB), le visualiseur reste sur le signal brut
        int   startMode = 0;      // START : 0 DIRECT, 1 = 1/4, 2 = 2/4, 3 = 1 BAR, 4 = 2 BAR (décompte avant la prise)
        bool  clickOn   = true;   // CLICK : métronome pendant le décompte
        int   recordMaxSeconds = 60; // DURATION : durée max de l'enregistrement en secondes (0 = illimité, garde 60 par sécurité)
    };
    Params p;

    // ── PRESETS : 8 compositions de réglages rappelables ────────────────────────────────────────
    // Vivantes ici (`presets_`), écrites dans settings.json par la couche shell (ui/settings_store).
    // Un emplacement vide se contente de garder ses défauts, `presetUsed_` dit s'il porte quelque chose.
    static constexpr int PRESET_N = 8;
    bool presetUsed(int i) const { return i >= 0 && i < PRESET_N && presetUsed_[i]; }
    int  presetSlot() const { return presetSlot_; }
    const Params& preset(int i) const {
        return presets_[i < 0 ? 0 : (i >= PRESET_N ? PRESET_N - 1 : i)];
    }
    void setPresetData(int i, const Params& v, bool used) {
        if (i < 0 || i >= PRESET_N) return;
        presets_[i] = v;
        presetUsed_[i] = used;
    }
    bool storePreset() {          // l'emplacement courant prend les réglages affichés
        if (presetSlot_ < 0 || presetSlot_ >= PRESET_N) return false;
        presets_[presetSlot_]  = p;
        presetUsed_[presetSlot_] = true;
        return true;
    }
    bool recallPreset() {         // l'emplacement courant remplace les réglages
        if (!presetUsed(presetSlot_)) return false;
        p = presets_[presetSlot_];
        dirty_ = true;
        return true;
    }
    bool clearPreset() {
        if (!presetUsed(presetSlot_)) return false;
        presetUsed_[presetSlot_] = false;
        return true;
    }

    static constexpr int   ROWS = 72, N = 600, FFTN = 4096, BINS = FFTN / 2;
    static constexpr float STEP = 0.05f, TMAX = N * STEP, FMIN = 200.f, FMAX = 5000.f;

    RecViz() { init(); reset(); }

    void setSampleRate(int sr) { sr_ = std::max(8000, sr); }
    void setAxis(Axis a) { p.axis = a; reset(); }

    void reset() {
        for (int k = 0; k < 3; ++k) {
            g_[k].assign(static_cast<size_t>(ROWS) * N, -100.f);
            bl_[k].assign(static_cast<size_t>(ROWS) * N, -100.f);
            sm_[k].assign(ROWS, -100.f);
        }
        std::fill(smL_.begin(), smL_.end(), 0.f);
        std::fill(smR_.begin(), smR_.end(), 0.f);
        head_ = N - 1; calib_ = 0; calMax_ = -100.f; started_ = false; dirty_ = true;
    }

    // Étalonnage du silence (1 s) : seuil = bruit max + 10 dB.
    void startCalibration() { calib_ = 20; calMax_ = -100.f; }
    bool calibrating() const { return calib_ > 0; }
    // Vrai dès que push() a reçu de l'audio (sert à diagnostiquer un câblage manquant).
    float peakDb() const { return 20.f * std::log10(std::max(peak_.load(std::memory_order_relaxed), 1e-6f)); }
    bool receiving() const { return armed_; }

    // THREAD AUDIO. r peut être nullptr (mono).
    void push(const float* l, const float* r, int frames) {
        uint64_t w = wpos_.load(std::memory_order_relaxed);
        float pk = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float mono = r ? 0.5f * (l[i] + r[i]) : l[i];
            ringL_[(w + i) & MASK] = p.stereo ? l[i] : mono;
            ringR_[(w + i) & MASK] = p.stereo && r ? r[i] : (p.stereo ? l[i] : mono);
            pk = std::max(pk, std::max(std::fabs(l[i]), r ? std::fabs(r[i]) : 0.f));
        }
        if (frames > 0) peak_.store(pk, std::memory_order_relaxed);
        wpos_.store(w + static_cast<uint64_t>(frames), std::memory_order_release);
    }

    // THREAD UI : alimente le visualiseur à partir du tampon d'enregistrement qui GRANDIT
    // (pointeurs L/R + nombre de frames depuis le début du REC). À appeler à chaque rafraîchissement
    // pendant le REC ; ne pousse que les frames nouvelles. R peut être nullptr (mono).
    void feedBuffer(const float* l, const float* r, int len, int sampleRate) {
        if (!l || len <= 0) return;
        if (!armed_ || len < fed_) {          // nouveau REC : on repart de zéro + étalonnage du silence
            setSampleRate(sampleRate);
            reset();
            if (p.autoCal) startCalibration();
            fed_ = 0;
            armed_ = true;
        }
        if (len > fed_) {
            push(l + fed_, r ? r + fed_ : nullptr, len - fed_);
            fed_ = len;
        }
    }
    // À appeler quand le REC est arrêté.
    void stopFeed() { armed_ = false; fed_ = 0; }


    // ---- Menu de réglages (bouton « J:SET » à l'écran / touche J) -------------------------------
    // Branche tes touches ainsi : toggleMenu() sur J ; quand menuOpen() : menuMove(±1) haut/bas,
    // menuAdjust(±1) gauche/droite (menuAdjust(±1, true) = pas x5). click(x, y) pour la souris.
    static constexpr int MENU_N = 15;   // VIZ 0..6, SAMPLE/REC 7..10, PRESETS 11..14
    static constexpr int VIZ_MENU_N = 7;
    static constexpr int MOONVIZ_MENU_N = 8;
    bool menuOpen() const { return menu_; }
    bool visualizerMenu() const { return visualizerOnly_; }
    int menuSelection() const { return menuSel_; }
    void toggleMenu() { menu_ = !menu_; visualizerOnly_ = false; }
    void openVisualizerMenu() { menu_ = true; visualizerOnly_ = true; menuSel_ = 0; }
    void closeMenu() { menu_ = false; visualizerOnly_ = false; }
    void menuMove(int d) {
        if (visualizerOnly_) {
            const int column = menuSel_ / 4;
            const int row = ((menuSel_ % 4) + d + 4) % 4;
            menuSel_ = column * 4 + row;
            return;
        }
        const int count = MENU_N;
        menuSel_ = ((menuSel_ + d) % count + count) % count;
    }
    void menuMoveHorizontal(int d) {
        if (!visualizerOnly_) return;
        menuSel_ = (menuSel_ + d * 4 + MOONVIZ_MENU_N) % MOONVIZ_MENU_N;
    }
    void menuAdjust(int d, bool big = false) {
        auto clampf = [](float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); };
        if (visualizerOnly_ && menuSel_ == 7) {
            p.stereo = !p.stereo;
            reset();
            dirty_ = true;
            return;
        }
        switch (menuSel_) {
            case 0: p.gate    = clampf(std::round(p.gate + d * (big ? 5 : 1)), -90.f, -10.f); break;
            case 1: p.timePow = clampf(std::round((p.timePow + d * 0.1f) * 10.f) / 10.f, 1.5f, 3.5f); break;
            case 2: p.bass    = clampf(std::round(p.bass + d * (big ? 5 : 1)), 0.f, 40.f); break;
            case 3: p.fall    = clampf(std::round((p.fall + d * 0.5f) * 2.f) / 2.f, 0.5f, 6.f); break;
            case 4: setAxis(p.axis == Axis::Pan ? Axis::Freq : Axis::Pan); break;
            case 5: p.spread  = clampf(std::round((p.spread + d * (big ? 1.0f : 0.2f)) * 10.f) / 10.f, 0.5f, 8.f); break;
            case 6: {
                const int track = ((p.track + 1 + d) % 10 + 10) % 10 - 1;
                if (track != p.track) {
                    p.track = track;
                    reset();
                }
                break;
            }
            case 7: p.micGainDb = clampf(std::round(p.micGainDb + d * (big ? 6 : 1)), 0.f, 36.f); break;
            case 8: p.startMode = ((p.startMode + d) % 5 + 5) % 5; break;
            case 9: p.clickOn = !p.clickOn; break;
            case 10: p.recordMaxSeconds = std::max(0, p.recordMaxSeconds + d * (big ? 10 : 1)); break;
            // PRESETS : 11 = l'emplacement, 12 = SAVE, 13 = LOAD, 14 = CLEAR (←/→ pour effacer).
            case 11: presetSlot_ = ((presetSlot_ + d) % PRESET_N + PRESET_N) % PRESET_N; break;
            case 12: storePreset(); break;
            case 13: recallPreset(); break;
            case 14: clearPreset(); break;
        }
        dirty_ = true;
    }
    // Clic souris (mêmes coordonnées que x, y passés à draw). Renvoie true si le bouton J:SET est touché.
    bool click(int mx, int my) {
        if (mx >= btn_[0] && mx < btn_[0] + btn_[2] && my >= btn_[1] && my < btn_[1] + btn_[3]) { toggleMenu(); return true; }
        return false;
    }

    // THREAD UI : un pas par 1/20 s d'audio reçu.
    void update() {
        const uint64_t w     = wpos_.load(std::memory_order_acquire);
        const uint64_t stepN = static_cast<uint64_t>(std::max(1, sr_ / 20));
        if (!started_) { nextEnd_ = w + stepN; started_ = true; }
        while (nextEnd_ <= w) {
            if (w - nextEnd_ > 32768) nextEnd_ = w;
            analyse(nextEnd_);
            nextEnd_ += stepN;
        }
    }

    // Un pas à partir de colonnes déjà calculées (3 couches en PAN, 2 en FREQ ; ROWS valeurs en dB).
    void stepRaw(const float* const* raws) {
        const bool f  = p.axis == Axis::Freq;
        const int  nl = f ? 2 : 3;
        if (calib_ > 0) {
            for (int k = 0; k < nl; ++k)
                for (int r = 0; r < ROWS; ++r) calMax_ = std::max(calMax_, raws[k][r]);
            if (--calib_ == 0) p.gate = std::min(-10.f, std::max(-90.f, std::round(calMax_ + 10.f)));
            dirty_ = true;
            return;
        }
        const int prev = head_;
        head_ = (head_ + 1) % N;
        for (int k = 0; k < nl; ++k) {
            for (int r = 0; r < ROWS; ++r) {
                const float gk = p.gate + ((f ? r < ROWS / 3 : k == 0) ? p.bass : 0.f);
                float v = raws[k][r];
                if (v < gk) v = -100.f;
                sm_[k][r] = std::max(v, sm_[k][r] - p.fall);
                g_[k][static_cast<size_t>(head_) * ROWS + r] = sm_[k][r];
            }
            blurCol(k, prev, true);
            blurCol(k, head_, false);
        }
        dirty_ = true;
    }

    template <class Cv>
    void draw(Cv& c, int x, int y, int w, int h, int cs, int fs,
              const Theme* theme = nullptr, bool showMenu = true) const;

  private:
    static constexpr uint64_t CAP = 65536, MASK = CAP - 1;
    static constexpr int      SUB = 16;
    std::vector<float>        ringL_, ringR_;
    std::atomic<uint64_t>     wpos_{0};
    std::atomic<float>        peak_{0.f};
    uint64_t                  nextEnd_ = 0;
    bool                      started_ = false;
    int                       sr_ = 48000;
    int                       fed_ = 0;
    bool                      menu_ = false;
    bool                      visualizerOnly_ = false;
    int                       menuSel_ = 0;
    Params                    presets_[PRESET_N];
    bool                      presetUsed_[PRESET_N] = {false};
    int                       presetSlot_ = 0;
    mutable int               btn_[4] = {0, 0, 0, 0};
    bool                      armed_ = false;
    std::vector<float>        win_, cos_, sin_, re_, im_, tbuf_, mag_, smL_, smR_, dbL_, dbR_;
    std::vector<int>          rev_;
    std::vector<float>        g_[3], bl_[3], sm_[3];
    int                       head_ = N - 1, calib_ = 0;
    float                     calMax_ = -100.f;

    struct Geo { int w = 0, h = 0, cs = 0, fs = 0, ml = 0, pw = 0, ph = 0, oy0 = 0, oy1 = 0, ch = 0; };
    struct Seg { float a0, b0, a1, b1; uint8_t t; };
    struct Run { int16_t x, y, len; uint8_t g; };
    mutable Geo                  geo_;
    mutable Params               built_;
    mutable bool                 dirty_ = true, haveBuilt_ = false;
    mutable std::vector<float>   lin_[3], colMax_[3], xtab_;
    mutable std::vector<Seg>     segs_[3];
    mutable std::vector<uint8_t> plane_;
    mutable std::vector<Run>     runs_;

    void init() {
        ringL_.assign(CAP, 0.f); ringR_.assign(CAP, 0.f);
        win_.resize(FFTN); cos_.resize(FFTN / 2); sin_.resize(FFTN / 2);
        re_.resize(FFTN); im_.resize(FFTN); tbuf_.resize(FFTN); mag_.resize(BINS);
        smL_.assign(BINS, 0.f); smR_.assign(BINS, 0.f); dbL_.assign(BINS, -200.f); dbR_.assign(BINS, -200.f);
        rev_.resize(FFTN);
        const double PI2 = 6.283185307179586;
        for (int n = 0; n < FFTN; ++n) {
            double a = PI2 * n / FFTN;
            win_[n] = static_cast<float>(0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2 * a));
        }
        for (int m = 0; m < FFTN / 2; ++m) {
            cos_[m] = static_cast<float>(std::cos(PI2 * m / FFTN));
            sin_[m] = static_cast<float>(std::sin(PI2 * m / FFTN));
        }
        int bits = 0; while ((1 << bits) < FFTN) ++bits;
        for (int i = 0; i < FFTN; ++i) {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev_[i] = r;
        }
    }

    void fftMag(const float* in, float* mag) {
        for (int i = 0; i < FFTN; ++i) { re_[rev_[i]] = in[i]; im_[rev_[i]] = 0.f; }
        for (int len = 2; len <= FFTN; len <<= 1) {
            const int half = len >> 1, stp = FFTN / len;
            for (int i = 0; i < FFTN; i += len)
                for (int k = 0; k < half; ++k) {
                    const float wr = cos_[k * stp], wi = -sin_[k * stp];
                    const int   a = i + k, b = a + half;
                    const float tr = wr * re_[b] - wi * im_[b], ti = wr * im_[b] + wi * re_[b];
                    re_[b] = re_[a] - tr; im_[b] = im_[a] - ti;
                    re_[a] += tr;         im_[a] += ti;
                }
        }
        for (int k = 0; k < BINS; ++k) mag[k] = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]) / FFTN;
    }

    void analyse(uint64_t e) {
        for (int ch = 0; ch < 2; ++ch) {
            const std::vector<float>& ring = ch ? ringR_ : ringL_;
            for (int n = 0; n < FFTN; ++n) {
                const int64_t pos = static_cast<int64_t>(e) - FFTN + n;
                const float   s   = pos < 0 ? 0.f : ring[static_cast<uint64_t>(pos) & MASK];
                tbuf_[n] = s * p.inputGain * win_[n];
            }
            fftMag(tbuf_.data(), mag_.data());
            std::vector<float>& sm = ch ? smR_ : smL_;
            std::vector<float>& db = ch ? dbR_ : dbL_;
            for (int k = 0; k < BINS; ++k) {
                sm[k] = 0.4f * sm[k] + 0.6f * mag_[k];
                db[k] = 20.f * std::log10(std::max(sm[k], 1e-10f));
            }
        }
        float raw[3][ROWS];
        if (p.axis == Axis::Pan) panCol3(raw); else bands(raw);
        const float* rp[3] = {raw[0], raw[1], raw[2]};
        stepRaw(rp);
    }

    void panCol3(float (*out)[ROWS]) const {
        const float nyq = sr_ * 0.5f;
        const float fe[4] = {40.f, 585.f, 1710.f, 12000.f};
        int ed[4];
        for (int m = 0; m < 4; ++m) ed[m] = std::min(BINS - 1, std::max(1, static_cast<int>(std::floor(fe[m] / nyq * BINS))));
        static const float W[7] = {.1f, .25f, .5f, 1.f, .5f, .25f, .1f};
        for (int l = 0; l < 3; ++l) {
            float E[ROWS] = {0};
            for (int i = ed[l]; i < ed[l + 1]; ++i) {
                const float dl = dbL_[i], dr = dbR_[i];
                if (dl < -75.f && dr < -75.f) continue;
                const float pl = std::pow(10.f, dl * 0.1f), pr = std::pow(10.f, dr * 0.1f);
                const float ml = std::sqrt(pl), mr = std::sqrt(pr);
                const int   idx = static_cast<int>(std::lround(((mr - ml) / (mr + ml + 1e-12f) + 1.f) * 0.5f * (ROWS - 1)));
                E[std::min(ROWS - 1, std::max(0, idx))] += pl + pr;
            }
            for (int r = 0; r < ROWS; ++r) {
                float o = 0.f;
                for (int k = -3; k <= 3; ++k) { const int rr = r + k; if (rr >= 0 && rr < ROWS) o += E[rr] * W[k + 3]; }
                out[l][r] = std::max(-100.f, 10.f * std::log10(o + 1e-10f));
            }
        }
    }

    void bands(float (*out)[ROWS]) const {
        const float nyq = sr_ * 0.5f;
        for (int ch = 0; ch < 2; ++ch) {
            const std::vector<float>& db = ch ? dbR_ : dbL_;
            for (int b = 0; b < ROWS; ++b) {
                const float f0 = FMIN * std::pow(FMAX / FMIN, static_cast<float>(b) / ROWS);
                const float f1 = FMIN * std::pow(FMAX / FMIN, static_cast<float>(b + 1) / ROWS);
                const int   i0 = static_cast<int>(std::floor(f0 / nyq * BINS));
                const int   i1 = std::max(i0 + 1, static_cast<int>(std::floor(f1 / nyq * BINS)));
                float m = -140.f;
                for (int i = i0; i < i1 && i < BINS; ++i) m = std::max(m, db[i]);
                out[ch][b] = m;
            }
        }
    }

    // Flou 3×3 d'UNE colonne de l'anneau (moyenne des voisins existants).
    void blurCol(int k, int c, bool hasRight) {
        int cols[3], nc = 0;
        cols[nc++] = (c + N - 1) % N; cols[nc++] = c; if (hasRight) cols[nc++] = (c + 1) % N;
        for (int r = 0; r < ROWS; ++r) {
            float s = 0.f; int n = 0;
            for (int ci = 0; ci < nc; ++ci)
                for (int dy = -1; dy <= 1; ++dy) {
                    const int rr = r + dy;
                    if (rr < 0 || rr >= ROWS) continue;
                    s += g_[k][static_cast<size_t>(cols[ci]) * ROWS + rr]; ++n;
                }
            bl_[k][static_cast<size_t>(c) * ROWS + r] = s / n;
        }
    }

    float xOf(float idx) const {
        float t = (idx + 1.f) * SUB;
        if (t < 0.f) t = 0.f;
        const int t0 = static_cast<int>(t);
        if (t0 >= static_cast<int>(xtab_.size()) - 1) return xtab_.back();
        return xtab_[t0] + (xtab_[t0 + 1] - xtab_[t0]) * (t - t0);
    }
    float yOf(int k, int pk, float bc) const {
        const int oy = pk ? geo_.oy1 : geo_.oy0;
        if (built_.axis == Axis::Pan) {
            // PAN : L en haut, R en bas, le centre L/R est le bord intérieur de chaque panneau.
            // SPREAD amplifie l'écart à ce centre (1 = d'origine, >1 = plus large/aéré).
            const float half  = ROWS / 2.f;
            const float inner = pk ? static_cast<float>(geo_.oy1)
                                   : static_cast<float>(geo_.oy0 + geo_.ph);
            const float d     = (pk ? (bc - half) : (half - bc)) / half;   // 0 = centre, 1 = bord
            return inner + (pk ? 1.f : -1.f) * d * built_.spread * geo_.ph;
        }
        const bool flip = (k == 0);
        return oy + (flip ? 1.f - bc / ROWS : bc / ROWS) * geo_.ph;
    }

    void plot(int px, int py, uint8_t g, int cl, int cr, int ct, int cb, bool dash) const {
        if (px < cl || px >= cr || py < ct || py >= cb) return;
        if (dash && (((px + py) / 3) & 1)) return;
        uint8_t& d = plane_[static_cast<size_t>(py) * geo_.w + px];
        if (g > d) d = g;
    }
    void line(int x0, int y0, int x1, int y1, uint8_t g, int cl, int cr, int ct, int cb, bool dash) const {
        const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        for (;;) {
            plot(x0, y0, g, cl, cr, ct, cb, dash);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    }

    // Marching squares sur la grille floutée ; bords = -inf (polygones fermés, comme d3).
    void contours(int k, const float th[4]) const {
        std::vector<Seg>& out = segs_[k];
        out.clear();
        const float* L = lin_[k].data();
        auto val = [&](int i, int j) -> float { return (i < 0 || i >= N || j < 0 || j >= ROWS) ? -1e4f : L[i * ROWS + j]; };
        const float lo = th[0];
        for (int i = -1; i < N; ++i) {
            if (i >= 0 && i + 1 < N && colMax_[k][i] < lo && colMax_[k][i + 1] < lo) continue;
            for (int j = -1; j < ROWS; ++j) {
                const float tl = val(i, j), tr = val(i + 1, j), br = val(i + 1, j + 1), bl = val(i, j + 1);
                const float mx = std::max(std::max(tl, tr), std::max(br, bl));
                if (mx < lo) continue;
                const float mn = std::min(std::min(tl, tr), std::min(br, bl));
                for (int t = 0; t < 4; ++t) {
                    const float T = th[t];
                    if (!(mn < T && mx >= T)) continue;
                    const int   code = (tl >= T ? 8 : 0) | (tr >= T ? 4 : 0) | (br >= T ? 2 : 0) | (bl >= T ? 1 : 0);
                    const float fT = (T - tl) / (tr - tl), fR = (T - tr) / (br - tr);
                    const float fB = (T - bl) / (br - bl), fL = (T - tl) / (bl - tl);
                    const float ax = static_cast<float>(i), ay = static_cast<float>(j);
                    // arêtes : 0 haut, 1 droite, 2 bas, 3 gauche
                    auto pt = [&](int e, float& a, float& b) {
                        if (e == 0)      { a = ax + fT; b = ay; }
                        else if (e == 1) { a = ax + 1;  b = ay + fR; }
                        else if (e == 2) { a = ax + fB; b = ay + 1; }
                        else             { a = ax;      b = ay + fL; }
                    };
                    auto seg = [&](int e0, int e1) {
                        float a0, b0, a1, b1;
                        pt(e0, a0, b0); pt(e1, a1, b1);
                        out.push_back({a0, b0, a1, b1, static_cast<uint8_t>(t)});
                    };
                    switch (code) {
                        case 1: case 14: seg(3, 2); break;
                        case 2: case 13: seg(2, 1); break;
                        case 3: case 12: seg(3, 1); break;
                        case 4: case 11: seg(0, 1); break;
                        case 6: case 9:  seg(0, 2); break;
                        case 7: case 8:  seg(0, 3); break;
                        case 5:
                            if ((tl + tr + br + bl) * 0.25f >= T) { seg(0, 3); seg(2, 1); } else { seg(0, 1); seg(3, 2); }
                            break;
                        case 10:
                            if ((tl + tr + br + bl) * 0.25f >= T) { seg(0, 1); seg(3, 2); } else { seg(0, 3); seg(2, 1); }
                            break;
                        default: break;
                    }
                }
            }
        }
    }

    void rebuild() const {
        const Geo& G = geo_;
        const bool f = built_.axis == Axis::Freq;
        const float th[4] = {built_.gate + 3, built_.gate + 15, built_.gate + 27, built_.gate + 39};
        const int   nl = f ? 2 : 3;

        xtab_.resize(static_cast<size_t>(N + 1) * SUB + 1);
        for (size_t t = 0; t < xtab_.size(); ++t) {
            const float idx = static_cast<float>(t) / SUB - 1.f;
            const float s   = std::max(0.f, (N - 1 - idx) * STEP);
            xtab_[t] = G.ml + G.pw * (1.f - std::pow(std::min(s / TMAX, 1.f), 1.f / built_.timePow));
        }

        for (int k = 0; k < nl; ++k) {
            lin_[k].resize(static_cast<size_t>(ROWS) * N);
            colMax_[k].resize(N);
            for (int i = 0; i < N; ++i) {
                const float* src = &bl_[k][static_cast<size_t>((head_ + 1 + i) % N) * ROWS];
                float* dst = &lin_[k][static_cast<size_t>(i) * ROWS];
                float m = -1e4f;
                for (int j = 0; j < ROWS; ++j) { dst[j] = src[j]; m = std::max(m, src[j]); }
                colMax_[k][i] = m;
            }
        }

        plane_.assign(static_cast<size_t>(G.w) * G.h, 0);

        struct Job { int k, pk, kind, lo, hi; };   // kind : 0 plein, 1 trait, 2 pointillés
        Job jobs[18]; int nj = 0;
        const int oy[2] = {G.oy0, G.oy1};
        if (!f) {
            for (int pk = 0; pk < 2; ++pk)
                for (int k = 0; k < 3; ++k) jobs[nj++] = {k, pk, k, oy[pk], oy[pk] + G.ph};
        } else {
            for (int k = 0; k < 2; ++k)
                for (int r = 0; r < 3; ++r) {
                    const float lo = r / 3.f, hi = (r + 1) / 3.f;
                    const bool  flip = (k == 0);
                    const float ya = oy[k] + (flip ? 1 - lo : lo) * G.ph, yb = oy[k] + (flip ? 1 - hi : hi) * G.ph;
                    jobs[nj++] = {k, k, r, static_cast<int>(std::floor(std::min(ya, yb))), static_cast<int>(std::ceil(std::max(ya, yb)))};
                }
        }

        bool segDone[3] = {false, false, false};
        for (int q = 0; q < nj; ++q) {
            const Job& J = jobs[q];
            const int  cl = G.ml, cr = G.ml + G.pw;
            const int  ct = std::max(J.lo, oy[J.pk]), cb = std::min(J.hi, oy[J.pk] + G.ph);
            if (J.kind == 0) {
                const float thr = th[0] + (!f ? built_.bass : 0.f);
                std::vector<float> jf(static_cast<size_t>(std::max(0, cb - ct)));
                for (int py = ct; py < cb; ++py) {
                    float bc;
                    if (!f) {
                        // Inverse exacte de yOf() en PAN (SPREAD appliqué) ; hors panneau = vide.
                        const float half = ROWS / 2.f;
                        const float d = J.pk ? (py + 0.5f - oy[J.pk]) / (built_.spread * G.ph)
                                             : (oy[J.pk] + G.ph - (py + 0.5f)) / (built_.spread * G.ph);
                        if (d < 0.f || d > 1.f) { jf[py - ct] = -1.f; continue; }
                        bc = J.pk ? half + d * half : half - d * half;
                    } else { const float tt = (py + 0.5f - oy[J.pk]) / G.ph; bc = (J.k == 0 ? 1.f - tt : tt) * ROWS; }
                    jf[py - ct] = bc - 0.5f;
                }
                const float* L = lin_[J.k].data();
                for (int px = 0; px < G.pw; ++px) {
                    const float u = (px + 0.5f) / G.pw;
                    const float s = TMAX * std::pow(1.f - u, built_.timePow);
                    const float fi = (N - 1) - s / STEP;
                    if (fi < 0.f) continue;
                    int i0 = static_cast<int>(fi); if (i0 > N - 2) i0 = N - 2;
                    const float fr = fi - i0;
                    if (std::max(colMax_[J.k][i0], colMax_[J.k][i0 + 1]) < thr) continue;
                    const float* c0 = L + static_cast<size_t>(i0) * ROWS;
                    const float* c1 = c0 + ROWS;
                    for (int py = ct; py < cb; ++py) {
                        const float j = jf[py - ct];
                        if (j < 0.f || j > ROWS - 1) continue;
                        int j0 = static_cast<int>(j); if (j0 > ROWS - 2) j0 = ROWS - 2;
                        const float fj = j - j0;
                        const float a = c0[j0] + (c0[j0 + 1] - c0[j0]) * fj, b = c1[j0] + (c1[j0 + 1] - c1[j0]) * fj;
                        if (a + (b - a) * fr >= thr) plane_[static_cast<size_t>(py) * G.w + cl + px] = 255;
                    }
                }
            } else {
                if (!segDone[J.k]) { contours(J.k, th); segDone[J.k] = true; }
                const bool dash = (J.kind == 2);
                for (const Seg& sg : segs_[J.k]) {
                    float gr = 255.f * (0.55f + 0.45f * sg.t / 3.f);
                    if (dash) gr *= 0.75f;
                    const int x0 = static_cast<int>(std::lround(xOf(sg.a0))), x1 = static_cast<int>(std::lround(xOf(sg.a1)));
                    const int y0 = static_cast<int>(std::lround(yOf(J.k, J.pk, sg.b0 + 0.5f)));
                    const int y1 = static_cast<int>(std::lround(yOf(J.k, J.pk, sg.b1 + 0.5f)));
                    if ((y0 < ct && y1 < ct) || (y0 >= cb && y1 >= cb)) continue;
                    line(x0, y0, x1, y1, static_cast<uint8_t>(gr), cl, cr, ct, cb, dash);
                }
            }
        }

        runs_.clear();
        for (int py = 0; py < G.h; ++py) {
            const uint8_t* row = &plane_[static_cast<size_t>(py) * G.w];
            for (int px = 0; px < G.w;) {
                const uint8_t g = row[px];
                if (!g) { ++px; continue; }
                int e = px + 1;
                while (e < G.w && row[e] == g) ++e;
                runs_.push_back({static_cast<int16_t>(px), static_cast<int16_t>(py), static_cast<int16_t>(e - px), g});
                px = e;
            }
        }
    }
};

template <class Cv>
void RecViz::draw(Cv& c, int x, int y, int w, int h, int cs, int fs,
                  const Theme* theme, bool showMenu) const {
    const uint32_t BACK = theme ? theme->vizBackground : 0xFF000000u;
    const uint32_t WAVE = theme ? theme->vizWave : 0xFFFFFFFFu;
    const uint32_t GRID = theme ? theme->vizCenterLine : 0xFF4D4D4Du;
    const uint32_t DIMTXT = theme ? theme->textParam : 0xFFB0B0B0u;
    const uint32_t TEXT = theme ? theme->textValue : 0xFFFFFFFFu;
    const uint32_t SELECT = theme ? theme->rowCursor : 0xFFFFFFFFu;
    const uint32_t SELECT_TEXT = theme ? theme->background : 0xFF000000u;
    auto tw  = [&](const std::string& s) { return static_cast<int>(Cv::text_width(s, cs, fs)); };
    auto txt = [&](const std::string& s, int tx, int ty, uint32_t col) { c.draw_text(s, x + tx, y + ty, col, cs, fs); };
    auto runColor = [&](uint8_t level) {
        const float k = (float)level / 255.0f;
        auto channel = [&](int shift) {
            const float a = (float)((BACK >> shift) & 0xFFu);
            const float b = (float)((WAVE >> shift) & 0xFFu);
            return (uint32_t)std::clamp((int)std::lround(a + (b - a) * k), 0, 255);
        };
        return 0xFF000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0);
    };

    // Disposition verticale (rien n'est écrit par-dessus les courbes) :
    //   bandeau haut : « L SPECTRUM » à gauche, seuils à droite
    //   panneau L
    //   couloir      : repères de temps
    //   panneau R
    //   bandeau bas  : « R SPECTRUM »
    Geo G;
    G.w = w; G.h = h; G.cs = cs; G.fs = fs; G.ch = 7 * fs;
    G.ml = tw("5k") + 9;
    const int mr = 6, strip = 3, gap = G.ch + 6;   // plus de titres : les panneaux prennent toute la hauteur
    G.pw = w - G.ml - mr;
    G.ph = (h - 2 * strip - gap) / 2;
    G.oy0 = strip;
    G.oy1 = strip + G.ph + gap;

    const bool changed = !haveBuilt_ || dirty_ || G.w != geo_.w || G.h != geo_.h || G.cs != geo_.cs || G.fs != geo_.fs ||
                         p.axis != built_.axis || p.gate != built_.gate || p.bass != built_.bass || p.timePow != built_.timePow ||
                         p.spread != built_.spread;
    if (changed) {
        geo_ = G; built_ = p; haveBuilt_ = true; dirty_ = false;
        rebuild();
    }
    const bool  f  = built_.axis == Axis::Freq;
    const int   ml = G.ml, pw = G.pw, ph = G.ph, oy[2] = {G.oy0, G.oy1};

    c.fill_rect(x, y, w, h, BACK);
    for (int q = 0; q < 2; ++q) c.stroke_rect(x + ml, y + oy[q], pw, ph, WAVE);

    // repères de temps dans le couloir central
    const int   labY = oy[0] + ph + (gap - G.ch) / 2;
    const float marks[4] = {30.f, 15.f, 5.f, 1.f};
    for (float sec : marks) {
        const int px = static_cast<int>(ml + pw * (1.f - std::pow(std::min(sec / TMAX, 1.f), 1.f / built_.timePow)));
        for (int q = 0; q < 2; ++q)
            for (int yy = 0; yy < ph; yy += 6) c.fill_rect(x + px, y + oy[q] + yy, 1, 2, GRID);
        const std::string lb = std::to_string(static_cast<int>(sec)) + "s";
        txt(lb, std::max(px - tw(lb) / 2, ml + 4), labY, DIMTXT);
    }
    txt("now", ml + pw - tw("now"), labY, DIMTXT);

    // formes : graves pleins, médiums continus, aigus pointillés
    for (const Run& r : runs_) {
        const uint32_t col = runColor(r.g);
        c.fill_rect(x + r.x, y + r.y, r.len, 1, col);
    }

    auto tick = [&](int yy, const std::string& lb) {
        if (!lb.empty()) txt(lb, ml - 6 - tw(lb), yy - G.ch / 2, DIMTXT);
        c.fill_rect(x + ml - 3, y + yy, 3, 1, WAVE);
    };
    if (f) {
        const float fr[5] = {200.f, 500.f, 1000.f, 2000.f, 5000.f};
        for (float v : fr) {
            const float pp = std::log(v / FMIN) / std::log(FMAX / FMIN);
            const std::string lb = v >= 1000.f ? std::to_string(static_cast<int>(v / 1000)) + "k" : std::to_string(static_cast<int>(v));
            tick(static_cast<int>(oy[0] + ph * (1.f - pp)), lb);
            tick(static_cast<int>(oy[1] + ph * pp), lb);
        }
        txt("L", ml + pw - tw("L") - 4, oy[0] + 2, DIMTXT);
        txt("R", ml + pw - tw("R") - 4, oy[1] + 2, DIMTXT);
    } else {
        tick(oy[0], "L"); tick(oy[0] + ph / 2, ""); tick(oy[0] + ph, "C");
        tick(oy[1], "C"); tick(oy[1] + ph / 2, ""); tick(oy[1] + ph, "R");
    }

    // Bouton « J:SET » (coin haut-droit du panneau L) + menu de réglages.
    if (showMenu) {
        const std::string bl = "SET";
        const int bw = tw(bl) + 6, bh = G.ch + 4, bx = ml + pw - bw - 2, by = oy[0] + 2;
        c.fill_rect(x + bx, y + by, bw, bh, menu_ ? SELECT : BACK);
        c.stroke_rect(x + bx, y + by, bw, bh, WAVE);
        txt(bl, bx + 3, by + 2, menu_ ? SELECT_TEXT : WAVE);
        btn_[0] = x + bx; btn_[1] = y + by; btn_[2] = bw; btn_[3] = bh;
    }
    if (showMenu && menu_) {
        auto f1 = [](float v) { char b[16]; std::snprintf(b, sizeof b, "%.1f", v); return std::string(b); };
        static const char* START_NAMES[5] = {"DIRECT", "1/4", "2/4", "1 BAR", "2 BAR"};
        // Deux sections : les réglages du VISUALISEUR, puis ceux du SAMPLE / REC. Les en-têtes ne sont
        // pas sélectionnables : menuSel_ ne compte que les réglages (0..MENU_N-1).
        static const char* VIZ_LBL[7]    = {"GATE", "TIME", "BASS", "TRAIL", "AXIS", "SPREAD", "SOURCE"};
        static const char* SAMPLE_LBL[4] = {"MIC GAIN", "START", "CLICK", "DURATION"};
        static const char* PRESET_LBL[4] = {"PRESET", "SAVE", "LOAD", "CLEAR"};
        const char* trackName = p.track < 0 ? "MASTER" : (p.track < 8 ? nullptr : "PREVIEW");
        const std::string source = trackName ? trackName : "TRACK " + std::to_string(p.track + 1);
        const std::string VAL[MENU_N] = {
            std::to_string(static_cast<int>(std::lround(p.gate))) + "DB", f1(p.timePow),
            std::to_string(static_cast<int>(std::lround(p.bass))) + "DB", f1(p.fall),
            p.axis == Axis::Pan ? "PAN" : "FREQ",
            "x" + f1(p.spread), source,
            "+" + std::to_string(static_cast<int>(std::lround(p.micGainDb))) + "DB",
            START_NAMES[std::max(0, std::min(4, p.startMode))], p.clickOn ? "ON" : "OFF",
            p.recordMaxSeconds <= 0 ? "INF" : (std::to_string(p.recordMaxSeconds) + "S"),
            std::to_string(presetSlot_ + 1) + "/8" + (presetUsed(presetSlot_) ? "*" : "-"),
            "STORE", presetUsed(presetSlot_) ? "GO" : "EMPTY", "CLEAR"};

        const int nRows = visualizerOnly_ ? 1 + VIZ_MENU_N : 3 + MENU_N;
        std::vector<std::string> lab(static_cast<size_t>(nRows));
        std::vector<int> item(static_cast<size_t>(nRows)); // index du réglage, -1 pour un en-tête
        int         n = 0;
        lab[n] = "VIZ";           item[n] = -1; ++n;
        for (int i = 0; i < VIZ_MENU_N; ++i) { lab[n] = VIZ_LBL[i]; item[n] = i; ++n; }
        if (!visualizerOnly_) {
            lab[n] = "SAMPLE / REC";  item[n] = -1; ++n;
            for (int i = 0; i < 4; ++i) { lab[n] = SAMPLE_LBL[i]; item[n] = 7 + i; ++n; }
            lab[n] = "PRESETS";        item[n] = -1; ++n;
            for (int i = 0; i < 4; ++i) { lab[n] = PRESET_LBL[i]; item[n] = 11 + i; ++n; }
        }

        const int rowH = G.ch + 4, pad = 6;
        const int fit  = std::max(3, (h - 2 * pad) / rowH);     // lignes qui tiennent dans le panneau
        const int vis  = std::min(nRows, fit);
        int selRow = 0;
        for (int r = 0; r < nRows; ++r) if (item[r] == menuSel_) selRow = r;
        // Fenêtre déroulante : elle suit le curseur, qui reste donc toujours visible.
        const int first = std::max(0, std::min(selRow - vis / 2, nRows - vis));

        const int bw = tw("MIC GAIN") + tw("2 BAR") + 4 * pad + 6, bh = vis * rowH + 2 * pad;
        const int bx = (w - bw) / 2, by = std::max(0, (h - bh) / 2);
        c.fill_rect(x + bx, y + by, bw, bh, BACK);
        c.stroke_rect(x + bx, y + by, bw, bh, WAVE);
        int ry = by + pad;
        for (int r = first; r < first + vis; ++r, ry += rowH) {
            if (item[r] < 0) {   // en-tête de section
                txt(lab[r], bx + pad, ry + 2, DIMTXT);
                c.fill_rect(x + bx + pad, y + ry + rowH - 4, bw - 2 * pad, 1, GRID);
                continue;
            }
            const bool sel = (item[r] == menuSel_);
            if (sel) c.fill_rect(x + bx + 2, y + ry, bw - 8, rowH, SELECT);
            const uint32_t col = sel ? SELECT_TEXT : TEXT;
            txt(lab[r], bx + pad + 2, ry + 2, col);
            const std::string& v = VAL[item[r]];
            txt(v, bx + bw - pad - 3 - tw(v), ry + 2, col);
        }
        // Barre de défilement : la fenêtre est plus courte que la liste.
        if (vis < nRows) {
            const int sbX = bx + bw - 4, top = by + pad, sh = vis * rowH;
            const int th  = std::max(10, sh * vis / nRows);
            const int ty  = top + (sh - th) * first / std::max(1, nRows - vis);
            c.fill_rect(x + sbX, y + top, 1, sh, GRID);
            c.fill_rect(x + sbX - 1, y + ty, 3, th, WAVE);
        }
    }

    // Ligne d'état (diagnostic) : niveau d'entrée reçu et seuil courant.
    if (receiving() && calib_ == 0) {
        const std::string st = "IN " + std::to_string(static_cast<int>(std::lround(peakDb()))) +
                               "DB  GATE " + std::to_string(static_cast<int>(std::lround(built_.gate)));
        txt(st, ml + 4, oy[0] + 3, DIMTXT);
    }

    if (receiving() && calib_ > 0) {
        const std::string m = "CALIBRATING... KEEP QUIET";
        txt(m, ml + (pw - tw(m)) / 2, oy[0] + (ph - G.ch) / 2, WAVE);
    }
}

// Instance unique : le thread audio y pousse (push), l'UI la dessine (update + draw).
inline RecViz& rec_viz() {
    static RecViz v;
    return v;
}

inline RecViz& moonviz_viz() {
    static RecViz v;
    return v;
}

}  // namespace pt::ui