// grain-synth.h -- GRAINSYNTH : voix granulaire TEMPS REEL (meme base que MOONGRANULAR).
// Aucune allocation, aucun verrou : tout l'etat est dans la voix. Appel : audio thread, 1 echantillon / tick.
//
// Branchement (voir README_DEPLOY.md) : dans engine-mix.cpp, a la place des 2 lectures
// `Playhead::read(...)` d'une voix GRAINSYNTH, on appelle `voice.grain.tick(...)`.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "grain-params.h"
#include "grain-viz-tap.h"   // g_vizTap : le moteur y additionne la sortie des voix pour le visualiseur

namespace grainfx {

// Reglages temps reel d'un instrument. POD, copie dans InstrumentParams (note-queue.h).
struct RT {
    bool    enabled   = false;   // vrai pour un instrument GRAINSYNTH
    // --- MEME BASE que MOONGRANULAR : Instrument::granular (0..255) ---
    uint8_t size      = 0x40;    // taille de grain  (grainSize)
    uint8_t density   = 0x80;    // grains / s       (grainDensity)
    uint8_t pitch     = 0x80;    // 0x80 = unite     (grainPitch)
    uint8_t count     = 0x20;    // polyphonie       (grainCount)
    uint8_t scanPos   = 0x00;    // tete de lecture  (scanPos)
    uint8_t scanSpeed = 0x40;    // avance           (scanSpeed)
    bool    reverse   = false;   // sens             (grainReverse)
    // --- SETGRANULAR (sous-ensemble "live") ---
    Params  fx;                  // seuls les P_* marques live=true sont lus ici
};

class Voice {
  public:
    static constexpr int MAXG = 40;

    void start(uint32_t seedSalt) {
        for (Grain& g : g_) g.on = false;
        rng_.seed(0xC0FFEEu ^ (seedSalt * 2654435761u));
        timer_ = 0.0; head_ = 0.0; last_ = -1.0; lastSemi_ = 0.0f; haveLast_ = false; started_ = false;
    }
    bool active() const { for (const Grain& g : g_) if (g.on) return true; return false; }

    // Un echantillon. L/R : l'echantillon source (R peut etre nullptr). [winStart, winEnd] : la zone jouable.
    // noteRate : images source par echantillon de sortie, deja module (hauteur de la note). sr : taux de sortie.
    inline void tick(const float* L, const float* R, double winStart, double winEnd, float noteRate,
                     const RT& p, float sr, float& outL, float& outR) {
        const double W = std::max(2.0, winEnd - winStart);
        if (!started_) {   // la tete demarre a scanPos dans la zone
            head_ = winStart + (double)p.scanPos / 255.0 * W;
            rng_.seed(rng_.s ^ ((uint32_t)p.fx.v[P_SEED] * 40503u + 1u));
            started_ = true;
        }
        // Tete de lecture : avance (ou figee), reste dans la zone.
        const bool frozen = p.fx.v[P_FREEZE] >= 128;
        if (!frozen) {
            const float k = (float)p.scanSpeed / 255.0f;
            head_ += (double)(k * k * 2.0f);
            while (head_ >= winEnd) head_ -= W;
        }

        // Ordonnanceur de grains.
        timer_ -= 1.0;
        if (timer_ <= 0.0) {
            const float dens     = densHz(p.density);
            const float interval = std::max(8.0f, sr / dens);
            timer_ += (double)interval * (1.0 + 0.15 * (double)rng_.bi());
            if (rng_.u() >= prob(p.fx.v[P_SKIP])) spawn(W, winStart, winEnd, noteRate, p, sr, interval);
        }

        // Somme des grains.
        float sl = 0.0f, sr_ = 0.0f;
        for (Grain& g : g_) {
            if (!g.on) continue;
            double q = g.pos;
            if (q >= winEnd) q = winStart + std::fmod(q - winStart, W);
            else if (q < winStart) q = winEnd - std::fmod(winStart - q, W);
            const int   i0 = (int)q;
            const int   i1 = (double)(i0 + 1) >= winEnd ? (int)winStart : i0 + 1;
            const float fr = (float)(q - (double)i0);
            const float a  = L[i0] + (L[i1] - L[i0]) * fr;
            const float b  = R ? R[i0] + (R[i1] - R[i0]) * fr : a;
            const float w  = window_at(p.fx.v[P_SHAPE], p.fx.v[P_SKEW], (float)g.age / (float)g.len) * g.amp;
            sl += a * w * g.panL;
            sr_ += b * w * g.panR;
            g.pos += g.step;
            if (++g.age >= g.len) g.on = false;
        }
        outL = sl; outR = sr_;
    }

  private:
    struct Grain {
        bool on = false; double pos = 0.0, step = 0.0; int age = 0, len = 1; float amp = 0.f, panL = 1.f, panR = 1.f;
    };
    Grain  g_[MAXG];
    Rng    rng_;
    double timer_ = 0.0, head_ = 0.0, last_ = -1.0;
    float  lastSemi_ = 0.0f;
    bool   haveLast_ = false, started_ = false;

    inline void spawn(double W, double winStart, double winEnd, float noteRate, const RT& p, float sr, float interval) {
        // Polyphonie : `count` 0..255 -> 1..MAXG grains. Pleine : le plus ancien est repris.
        const int maxG = 1 + ((int)p.count * (MAXG - 1)) / 255;
        int slot = -1, active = 0, oldest = 0, oldAge = -1;
        for (int i = 0; i < MAXG; ++i) {
            if (g_[i].on) { ++active; if (g_[i].age > oldAge) { oldAge = g_[i].age; oldest = i; } }
            else if (slot < 0) slot = i;
        }
        if (active >= maxG || slot < 0) slot = oldest;

        const float sprayFrac = std::pow(prob(p.fx.v[P_SPRAY]), 2.0f) * 0.5f;
        double start; float semi;
        if (haveLast_ && rng_.u() < prob(p.fx.v[P_STUTR])) { start = last_; semi = lastSemi_; }
        else {
            start = head_ + (double)(rng_.bi() * sprayFrac) * W;
            // hauteur de base = grainPitch de MOONGRANULAR (0x80 = unite), puis les effets SETGRANULAR
            semi = grain_semitones(p.fx, rng_, (p.pitch - 128) * (24.0f / 128.0f) - semis(p.fx.v[P_PITCH]));
            last_ = start; lastSemi_ = semi; haveLast_ = true;
        }
        Grain& g = g_[slot];
        // grainReverse (MOONGRANULAR) = sens de base ; REV = probabilite d'inverser ce sens, grain par grain.
        const int  dir = (p.reverse != (rng_.u() < prob(p.fx.v[P_REV]))) ? -1 : 1;
        g.on  = true;
        g.pos = start;
        g.step = (double)dir * (double)noteRate * (double)std::pow(2.0f, semi / 12.0f);
        g.len = std::max(16, (int)(sizeMs(p.size) * sr / 1000.0f));
        g.age = 0;
        const float overlap = (float)g.len / interval;
        g.amp = std::max(0.25f, std::min(1.5f, 1.0f / std::sqrt(std::max(0.01f, overlap * 0.375f))));
        const float pan = rng_.bi() * prob(p.fx.v[P_WIDTH]);              // -1..1
        const float ang = (pan + 1.0f) * 0.7853981634f;                    // 0..pi/2
        g.panL = std::cos(ang) * 1.41421356f; g.panR = std::sin(ang) * 1.41421356f;
        (void)winStart; (void)winEnd;
    }
};

}  // namespace grainfx
