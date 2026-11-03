// grain-viz-tap.h -- pont thread audio -> thread UI pour le visualiseur de GRAINSYNTH.
// Les voix additionnent leur sortie par bloc (add), le moteur vide le bloc dans l'anneau (flush) ;
// l'UI relit l'anneau (drain). Sans allocation, sans verrou. Une seule paire producteur/consommateur.
#pragma once

#include <atomic>
#include <cstdint>

namespace grainfx {

struct VizTap {
    static constexpr uint32_t CAP = 1u << 16, MASK = CAP - 1, BLOCK_MAX = 8192;
    float l[CAP], r[CAP];
    float accL[BLOCK_MAX], accR[BLOCK_MAX];
    std::atomic<uint64_t> w{0};
    uint64_t rd = 0;                       // consommateur (UI) seulement

    VizTap() { for (uint32_t i = 0; i < CAP; ++i) l[i] = r[i] = 0.0f; clearAcc(); }
    void clearAcc() { for (uint32_t i = 0; i < BLOCK_MAX; ++i) accL[i] = accR[i] = 0.0f; }

    // AUDIO : une voix ajoute son echantillon `frameInBlock` du bloc courant.
    inline void add(int frameInBlock, float a, float b) {
        if ((uint32_t)frameInBlock < BLOCK_MAX) { accL[frameInBlock] += a; accR[frameInBlock] += b; }
    }
    // AUDIO : en fin de bloc (apres TOUTES les voix). Rien a ecrire si aucune voix n'a ajoute.
    inline void flush(int numFrames, bool anyVoice) {
        if (!anyVoice) return;
        const int n = numFrames > (int)BLOCK_MAX ? (int)BLOCK_MAX : numFrames;
        uint64_t x = w.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { l[(x + i) & MASK] = accL[i]; r[(x + i) & MASK] = accR[i]; accL[i] = accR[i] = 0.0f; }
        w.store(x + (uint64_t)n, std::memory_order_release);
    }
    // UI : f(const float* l, const float* r, int n) par morceaux de 512.
    template <class F> void drain(F&& f) {
        const uint64_t e = w.load(std::memory_order_acquire);
        if (e - rd > CAP / 2) rd = e - CAP / 2;       // trop en retard : on saute
        float bl[512], br[512];
        while (rd < e) {
            const int n = (int)(e - rd > 512 ? 512 : e - rd);
            for (int i = 0; i < n; ++i) { bl[i] = l[(rd + i) & MASK]; br[i] = r[(rd + i) & MASK]; }
            rd += (uint64_t)n;
            f(bl, br, n);
        }
    }
};

inline VizTap g_vizTap;

}  // namespace grainfx
