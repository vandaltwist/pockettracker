#pragma once
// granular-voice.h -- the live grain cloud of a MOONGRANULAR instrument.
//
// POD parameters only: this header is reached by the audio thread through note-queue.h, so it must
// not pull in songcore/model.h. `GranularSettings` mirrors Instrument::GranularParams one for one.
//
// The cloud is owned by a sampler Voice and replaces its single playhead read. Nothing allocates:
// MAX_GRAINS fixed slots, a dirty cheap LCG, and Ramp smoothing from effects/glide.h.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../effects/glide.h"

namespace pt {

// The engine's copy of the UI's GRAIN / ROAM / MOTION blocks. All bytes are 00-FF, like the cells.
struct GranularSettings {
    struct Grain {
        bool state     = true;
        int  position  = 0x00;   // read head, 0..255 of the sample
        int  size      = 0x40;   // grain length, mapped to 1..1000 ms
        int  density   = 0x80;   // grains per second, mapped to 1..200
        int  spray     = 0x00;   // random spread around the head
        int  pitch     = 0x80;   // 0x80 = unison, +-24 semitones
    } grain;
    struct Roam {
        bool state     = false;
        int  mode      = 0;      // BOUNCE / LOOP / PINGPONG / RANDOM
        int  range     = 0x00;
        int  wander    = 0x00;
        int  speed     = 0x80;
        int  smooth    = 0x40;
    } roam;
    struct Motion {
        bool state     = false;
        int  jump      = 0x00;
        int  hold      = 0x00;
        int  rev       = 0x00;
        int  elastic   = 0x00;
        int  chaos     = 0x00;
    } motion;
};

// One Hann-windowed grain. `pos` walks the sample at `step` frames per output sample.
struct GranularGrain {
    bool   on   = false;
    double pos  = 0.0;
    double step = 1.0;
    int    age  = 0;
    int    life = 1;
};

class GranularVoice {
public:
    static constexpr int MAX_GRAINS = 48;

    void reset() {
        for (GranularGrain& g : grains_) g.on = false;
        running_ = false;
        enabled_ = false;
        timer_   = 0.0;
        rng_     = 0x9E3779B9u;
        startOverride_ = -1;
    }

    void start(uint32_t seed, int length, float sampleRate) {
        reset();
        length_     = std::max(length, 2);
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        rng_        = 0x9E3779B9u ^ (seed * 2654435761u);
        if (rng_ == 0) rng_ = 1;
        running_    = true;
        enabled_    = true;
        startOverride_ = -1;
        // The first grain lands on the next frame; the ramps were just reset.
        timer_ = 0.0;
    }

    // Once per piece, from the published InstrumentParams. `sounding` says whether the ramps glide
    // (a note already running) or land on the value (the first piece). `startOverride` (0..255, or
    // -1) overrides POSITION when a slice is selected (slice playback / THRU / CUT).
    void setParams(const GranularSettings& p, int length, float noteRate, bool sounding, int frames,
                   float sampleRate, int startOverride = -1) {
        if (!running_) return;
        length_     = std::max(length, 2);
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        noteRate_   = noteRate > 0.0f ? noteRate : 1.0f;
        enabled_    = p.grain.state;

        if (startOverride >= 0) startOverride_ = startOverride;
        const float pos = (startOverride_ >= 0)
            ? (float)std::clamp(startOverride_, 0, 255) / 255.0f
            : (float)std::clamp(p.grain.position, 0, 255) / 255.0f;
        position_.set(pos, sounding, frames);
        size_.set((float)std::clamp(p.grain.size, 0, 255), sounding, frames);
        density_.set((float)std::clamp(p.grain.density, 0, 255), sounding, frames);
        spray_.set((float)std::clamp(p.grain.spray, 0, 255) / 255.0f, sounding, frames);
        pitch_.set((float)std::clamp(p.grain.pitch, 0, 255), sounding, frames);
    }

    // One output frame. The cloud sums every live grain into `outL`/`outR`.
    void render(const float* left, const float* right, float& outL, float& outR) {
        outL = 0.0f;
        outR = 0.0f;
        if (!running_ || !enabled_ || !left || length_ < 2) return;

        const float density = 1.0f + density_.next() * (199.0f / 255.0f);
        const double interval = (double)sampleRate_ / (double)std::max(density, 0.25f);
        timer_ -= 1.0;
        if (timer_ <= 0.0) {
            spawn(left, right);
            timer_ += interval;
            if (timer_ < 0.0) timer_ = 0.0;
        }

        float sumL = 0.0f, sumR = 0.0f;
        for (GranularGrain& g : grains_) {
            if (!g.on) continue;
            const int idx = (int)g.pos;
            if (idx < 0 || idx >= length_ - 1) {
                g.on = false;
                continue;
            }
            const float frac = (float)(g.pos - (double)idx);
            const float w    = 0.5f - 0.5f * cosf(6.28318530718f * (float)g.age / (float)g.life);
            const float l    = left[idx] + (left[idx + 1] - left[idx]) * frac;
            const float r    = right ? (right[idx] + (right[idx + 1] - right[idx]) * frac) : l;
            sumL += l * w;
            sumR += r * w;
            g.pos += g.step;
            if (++g.age >= g.life) g.on = false;
        }
        outL = sumL;
        outR = right ? sumR : sumL;
    }

    // The grains that are sounding now, as 0..1 of the sample, for the editor's overlay.
    int collect(float* out, int max) const {
        int n = 0;
        for (const GranularGrain& g : grains_) {
            if (!g.on || n >= max) continue;
            out[n++] = std::clamp((float)(g.pos / (double)std::max(length_, 1)), 0.0f, 1.0f);
        }
        return n;
    }

    bool running() const { return running_; }

private:
    uint32_t nextRandom() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    void spawn(const float* left, const float* right) {
        (void)left;  // intentionally unused
        (void)right; // intentionally unused
        // The oldest free slot, else the nearest to its end (it is quietest there).
        GranularGrain* dst = nullptr;
        for (GranularGrain& g : grains_) {
            if (!g.on) { dst = &g; break; }
        }
        if (!dst) {
            dst = &grains_[0];
            for (GranularGrain& g : grains_) {
                if (g.age > dst->age) dst = &g;
            }
        }

        const float position = position_.next();
        const float spray    = spray_.next() * 0.5f;
        const float sizeByte = size_.next();
        const int   lifeMs   = 1 + (int)(sizeByte * (999.0f / 255.0f));
        const int   life     = std::max(2, (int)((float)lifeMs * sampleRate_ / 1000.0f));

        double start = (double)position * (double)(length_ - 2);
        if (spray > 0.0f) {
            const double r = (double)nextRandom() / 4294967295.0;   // 0..1
            start += (r * 2.0 - 1.0) * (double)spray * (double)length_;
        }
        start = std::clamp(start, 0.0, (double)(length_ - 2));

        const float semis = ((pitch_.next() - 128.0f) / 128.0f) * 24.0f;
        const float pitch = powf(2.0f, semis / 12.0f);

        dst->on   = true;
        dst->pos  = start;
        dst->step = (double)noteRate_ * (double)pitch;
        dst->age  = 0;
        dst->life = life;
    }

    GranularGrain grains_[MAX_GRAINS];
    int     length_     = 0;
    float   sampleRate_ = 48000.0f;
    float   noteRate_   = 1.0f;
    bool    running_    = false;
    bool    enabled_    = false;
    double  timer_      = 0.0;
    uint32_t rng_       = 0x9E3779B9u;
    int      startOverride_ = -1;
    Ramp    position_, size_, density_, spray_, pitch_;
};

}  // namespace pt
