#pragma once
// transient_detect.h -- onset detector for the TRANSIENT slice method (header only, no allocation
// beyond its own scratch vectors, UI thread).
//
//   log-magnitude spectral flux  ->  local-median adaptive threshold  ->  peak picking with a minimum
//   gap  ->  the marker is moved back onto the attack's start, then onto the nearest zero crossing.
//
// `sensitivity` is the SENS cell, 0..255: higher = lower threshold and shorter minimum gap = MORE markers.
// The result is sorted, unique, inside [1, n), at most `maxOut` (the strongest are kept).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace songcore {

namespace td_detail {

// In-place radix-2 FFT of `n` (power of two) complex values.
inline void fft(std::vector<float>& re, std::vector<float>& im) {
    const int n = (int)re.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -6.283185307179586 / len;
        const float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

inline float median_of(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + m, v.end());
    return v[m];
}

}  // namespace td_detail

inline std::vector<int> detect_transient_markers(const float* x, int n, int sampleRate, int sensitivity,
                                                 int maxOut = 128) {
    std::vector<int> out;
    if (!x || n < 4096 || sampleRate <= 0 || maxOut <= 0) return out;
    const float s = std::clamp(sensitivity, 0, 255) / 255.0f;

    constexpr int N = 1024, HOP = 256, BINS = N / 2;
    const int frames = (n - N) / HOP + 1;
    if (frames < 8) return out;

    // ---- log-magnitude spectral flux (half-wave rectified), up to ~12 kHz --------------------
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * (float)std::cos(6.283185307179586 * i / N);
    const int maxBin = std::min(BINS, (int)(12000.0 * N / sampleRate));
    std::vector<float> re(N), im(N), prev(BINS, 0.0f), flux(frames, 0.0f);
    for (int f = 0; f < frames; ++f) {
        const float* p = x + (size_t)f * HOP;
        for (int i = 0; i < N; ++i) { re[i] = p[i] * win[i]; im[i] = 0.0f; }
        td_detail::fft(re, im);
        float sum = 0.0f;
        for (int b = 1; b < maxBin; ++b) {
            const float mag = std::sqrt(re[b] * re[b] + im[b] * im[b]);
            const float lg  = std::log1p(mag * 40.0f);   // compression: quiet onsets count as much as loud ones
            if (f > 0) sum += std::max(0.0f, lg - prev[b]);
            prev[b] = lg;
        }
        flux[f] = sum;
    }

    // ---- normalise: 95th percentile = 1 ------------------------------------------------------
    {
        std::vector<float> sorted(flux);
        std::sort(sorted.begin(), sorted.end());
        const float p95 = sorted[(size_t)((frames - 1) * 0.95)];
        // A steady tone or silence has a tiny flux whose ripple would be blown up by the normalisation:
        // below FLOOR the scale stays put, so the ripple stays under the threshold. (drums ~75, noise ~90, tone ~3)
        const float scale = std::max(p95, 20.0f);
        for (float& v : flux) v /= scale;
    }

    // ---- adaptive threshold + peak picking ---------------------------------------------------
    const int   half     = std::max(4, (int)(0.30 * sampleRate / HOP));          // +-300 ms
    const float delta    = 0.18f + 0.80f * std::pow(1.0f - s, 1.5f);
    const int   minGap   = std::max(2, (int)((0.030f + 0.150f * (1.0f - s)) * sampleRate / HOP));
    const int   nb       = 3;                                                     // local-maximum radius, frames
    struct Cand { int frame; float strength; };
    std::vector<Cand> cands;
    std::vector<float> win2;
    for (int f = 1; f < frames - 1; ++f) {
        if (flux[f] <= 0.0f) continue;
        bool isMax = true;
        for (int k = std::max(0, f - nb); k <= std::min(frames - 1, f + nb) && isMax; ++k)
            if (k != f && (flux[k] > flux[f] || (flux[k] == flux[f] && k < f))) isMax = false;
        if (!isMax) continue;
        win2.assign(flux.begin() + std::max(0, f - half), flux.begin() + std::min(frames, f + half + 1));
        const float med = td_detail::median_of(win2);
        if (flux[f] > med + delta) cands.push_back({f, flux[f] - med});
    }
    // minimum gap: stronger one wins
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.strength > b.strength; });
    std::vector<Cand> kept;
    for (const Cand& c : cands) {
        bool ok = true;
        for (const Cand& k : kept) if (std::abs(k.frame - c.frame) < minGap) { ok = false; break; }
        if (ok) kept.push_back(c);
        if ((int)kept.size() >= maxOut) break;
    }

    // ---- put each marker on the start of its attack ------------------------------------------
    // Frame f covers [f*HOP, f*HOP+N): the flux peaks when the onset is inside it. Look at a short-term
    // RMS envelope around it and take the steepest rise, then the nearest zero crossing before it.
    constexpr int B = 32;
    for (const Cand& c : kept) {
        const int lo = std::max(0, c.frame * HOP - HOP);
        const int hi = std::min(n - 2 * B, c.frame * HOP + N);
        if (hi - lo < 4 * B) continue;
        const int nbk = (hi - lo) / B;
        std::vector<float> env((size_t)nbk);
        for (int b = 0; b < nbk; ++b) {
            double e = 0.0;
            for (int i = 0; i < B; ++i) { const float v = x[lo + b * B + i]; e += (double)v * v; }
            env[(size_t)b] = (float)std::sqrt(e / B);
        }
        int   best = 0;
        float bestRise = -1.0f;
        for (int b = 0; b + 2 < nbk; ++b) {
            const float rise = std::log((env[(size_t)b + 2] + 1e-5f) / (env[(size_t)b] + 1e-5f));
            if (rise > bestRise) { bestRise = rise; best = b; }
        }
        int pos = lo + best * B;
        // zero crossing at or before `pos` (within 256 frames)
        for (int i = pos; i > std::max(1, pos - 256); --i)
            if ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f)) { pos = i; break; }
        if (pos >= 1 && pos < n - 1) out.push_back(pos);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // two markers can land on the same attack after refinement: keep the minimum gap
    std::vector<int> final;
    const int gapFrames = (int)((0.015f + 0.05f * (1.0f - s)) * sampleRate);
    for (int p : out) if (final.empty() || p - final.back() >= gapFrames) final.push_back(p);
    return final;
}

}  // namespace songcore
