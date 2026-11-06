// grain-fx.h -- SETGRANULAR : effet granulaire HORS-LIGNE, applique a un canal mono EN PLACE.
// Meme contrat que le reste de AudioEngine::applySampleFx : meme longueur en sortie, un canal a la fois.
// Thread UI uniquement (alloue des std::vector) : ne jamais l'appeler depuis le thread audio.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "grain-params.h"

namespace grainfx {

// buf : len echantillons, modifies sur place.  channel : 0 = gauche (ou mono), 1 = droite.
inline void process_channel(float* buf, int len, float sr, const Params& p, int channel) {
    if (!buf || len < 64 || sr < 8000.0f) return;

    const std::vector<float> src(buf, buf + len);
    std::vector<float> out((size_t)len, 0.0f);

    // Meme graine pour les 2 canaux tant que WIDTH = 0 : image stereo conservee.
    Rng rng;
    rng.seed(0x1234567u * (uint32_t)(p.v[P_SEED] + 1) + (p.v[P_WIDTH] > 0 ? (uint32_t)channel * 7919u : 0u));

    const float sizeSmp  = std::max(16.0f, sizeMs(p.v[P_SIZE]) * sr / 1000.0f);
    const float interval = std::max(8.0f, sr / densHz(p.v[P_DENS]));
    const bool  frozen   = p.v[P_FREEZE] >= 128;
    const float speed    = frozen ? 0.0f : scanSpeed(p.v[P_SCAN]);
    const float spray    = std::pow(prob(p.v[P_SPRAY]), 2.0f) * 0.5f * (float)len;
    const float widthOff = prob(p.v[P_WIDTH]) * 0.05f * (float)len * (channel ? 1.0f : -1.0f);
    // Niveau moyen conserve : un nuage de `overlap` grains (fenetre ~0.375 de puissance moyenne).
    const float overlap  = sizeSmp / interval;
    const float gainNorm = std::max(0.25f, std::min(2.0f, 1.0f / std::sqrt(std::max(0.01f, overlap * 0.375f))));

    double head  = prob(p.v[P_POS]) * (double)(len - 1);
    double timer = 0.0;
    double lastStart = head;
    float  lastSemi  = 0.0f;
    bool   haveLast  = false;

    for (int n = 0; n < len; ++n) {
        head += speed;
        while (head >= len) head -= len;
        while (head < 0)    head += len;

        if (--timer > 0.0) continue;
        timer += interval * (1.0 + 0.15 * rng.bi());

        if (rng.u() < prob(p.v[P_SKIP])) continue;

        double start;
        float  semi;
        if (haveLast && rng.u() < prob(p.v[P_STUTR])) { start = lastStart; semi = lastSemi; }
        else {
            start = head + (double)(rng.bi() * spray) + (double)widthOff;
            semi  = grain_semitones(p, rng);
            lastStart = start; lastSemi = semi; haveLast = true;
        }
        const float rate = std::pow(2.0f, semi / 12.0f);
        const int   dir  = (rng.u() < prob(p.v[P_REV])) ? -1 : 1;
        const int   glen = (int)sizeSmp;

        double pos = start;
        for (int k = 0; k < glen; ++k) {
            const int outIdx = n + k;
            if (outIdx >= len) break;
            double q = std::fmod(pos, (double)len);
            if (q < 0) q += len;
            const int   i0 = (int)q, i1 = (i0 + 1 >= len) ? 0 : i0 + 1;
            const float fr = (float)(q - i0);
            const float s  = src[(size_t)i0] + (src[(size_t)i1] - src[(size_t)i0]) * fr;
            const float w  = window_at(p.v[P_SHAPE], p.v[P_SKEW], (float)k / (float)glen);
            out[(size_t)outIdx] += s * w * gainNorm;
            pos += (double)dir * rate;
        }
    }

    // ---- FOLLOW : le resultat suit l'enveloppe du son d'origine ---------------------------------
    if (p.v[P_FOLLOW] > 0) {
        const float a = std::exp(-1.0f / (0.005f * sr)), r = std::exp(-1.0f / (0.050f * sr));
        std::vector<float> env((size_t)len);
        float e = 0.0f, peak = 1e-6f;
        for (int i = 0; i < len; ++i) {
            const float x = std::fabs(src[(size_t)i]);
            e = x > e ? a * e + (1.0f - a) * x : r * e + (1.0f - r) * x;
            env[(size_t)i] = e; peak = std::max(peak, e);
        }
        const float k = prob(p.v[P_FOLLOW]);
        for (int i = 0; i < len; ++i) out[(size_t)i] *= (1.0f - k) + k * (env[(size_t)i] / peak);
    }

    // ---- GATE : hachage aleatoire (cellules d'environ 1/16 s, rampes de 2 ms) -------------------
    if (p.v[P_GATE] > 0) {
        const int   cell = std::max(32, (int)(sr / 16.0f));
        const float ramp = 1.0f / std::max(1.0f, 0.002f * sr);
        float g = 1.0f, target = 1.0f;
        for (int i = 0; i < len; ++i) {
            if (i % cell == 0) target = (rng.u() < prob(p.v[P_GATE]) * 0.8f) ? 0.0f : 1.0f;
            g += std::max(-ramp, std::min(ramp, target - g));
            out[(size_t)i] *= g;
        }
    }

    // ---- DLY : delay a feedback sur le nuage -------------------------------------------------------
    if (p.v[P_DLYFB] > 0) {
        const int   d  = std::max(1, (int)(dlyMs(p.v[P_DLYT]) * sr / 1000.0f));
        const float fb = std::min(0.92f, prob(p.v[P_DLYFB]) * 0.92f);
        for (int i = d; i < len; ++i) out[(size_t)i] += out[(size_t)(i - d)] * fb;
    }

    // ---- CRUSH / DRIVE -----------------------------------------------------------------------------
    if (p.v[P_CRUSH] > 0) {
        const float bits = 16.0f - prob(p.v[P_CRUSH]) * 14.0f;
        const float lv   = std::pow(2.0f, bits - 1.0f);
        for (float& x : out) x = std::round(x * lv) / lv;
    }
    if (p.v[P_DRIVE] > 0) {
        const float k = 1.0f + prob(p.v[P_DRIVE]) * 8.0f, nrm = std::tanh(k);
        for (float& x : out) x = std::tanh(x * k) / nrm;
    }

    // ---- LOCUT / HICUT : filtres a un pole -----------------------------------------------------------
    if (p.v[P_LOCUT] > 0) {
        const float fc = 20.0f * std::pow(100.0f, prob(p.v[P_LOCUT]));
        const float a  = std::exp(-6.283185307f * fc / sr);
        float lp = 0.0f;
        for (float& x : out) { lp = (1.0f - a) * x + a * lp; x -= lp; }
    }
    if (p.v[P_HICUT] > 0) {
        const float fc = 20000.0f * std::pow(0.01f, prob(p.v[P_HICUT]));
        const float a  = std::exp(-6.283185307f * std::min(fc, sr * 0.45f) / sr);
        float lp = 0.0f;
        for (float& x : out) { lp = (1.0f - a) * x + a * lp; x = lp; }
    }

    // ---- MIX / LEVEL -----------------------------------------------------------------------------------
    const float mix = prob(p.v[P_MIX]), lvl = p.v[P_LEVEL] / 128.0f;
    for (int i = 0; i < len; ++i) {
        float y = (src[(size_t)i] * (1.0f - mix) + out[(size_t)i] * mix) * lvl;
        buf[i] = std::isfinite(y) ? std::max(-4.0f, std::min(4.0f, y)) : 0.0f;   // le limiteur du moteur finit le travail
    }
}

}  // namespace grainfx
