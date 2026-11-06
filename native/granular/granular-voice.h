#pragma once
// granular-voice.h -- the live grain cloud of a MOONGRANULAR instrument (v2).
//
// POD parameters only: this header is reached by the audio thread through note-queue.h, so it must
// not pull in songcore/model.h. `GranularSettings` mirrors Instrument::GranularParams one for one.
//
// v2: every cell of the GRAIN / ROAM / MOTION blocks now reaches the sound, and every parameter
// can take a modulation source (LFO, S&H random, or a BUS = sidechain of a track / a message).
//   - GRAIN  : position size density spray pitch + TIME(bpm sync) WINDOW SPREAD G-ATTACK G-DECAY G-PAN P-JITTER
//   - ROAM   : the read head travels by itself (BOUNCE / LOOP / DRIFT / RANDOM) -- see updateControl()
//   - MOTION : per-grain probabilities (jump hold rev gap) + elastic chaos stutter tapestop tunnel spin
//   - MODS   : GranularSettings::Mod x8 -- one row = TARGET <- SOURCE x AMOUNT (the "double-click" popup edits these)
// Nothing allocates: MAX_GRAINS fixed slots, xorshift RNG, Ramp smoothing from effects/glide.h.
// Every new field defaults to "off", so a project saved before v2 sounds exactly as it did.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../effects/glide.h"

namespace pt {

// ---- Modulation vocabulary (stored in the project BY VALUE: never reorder, only append) ----------
enum GranTarget : int {
    GT_NONE = 0, GT_POSITION, GT_SIZE, GT_DENSITY, GT_SPRAY, GT_PITCH, GT_JITTER, GT_REV, GT_JUMP,
    GT_CHAOS, GT_STEREO, GT_ROAM_SPEED, GT_ROAM_RANGE, GT_LEVEL,
    // Appended: a project written before STRETCH keeps its byte values.
    GT_STRETCH, GT_GRAIN_WINDOW, GT_GRAIN_SPREAD, GT_GRAIN_ATTACK, GT_GRAIN_DECAY, GT_GRAIN_MIX,
    GT_ROAM_WANDER, GT_ROAM_SMOOTH, GT_ROAM_STEP_RATE, GT_ROAM_INERTIA, GT_ROAM_TARGET,
    GT_ROAM_TLOCK, GT_ROAM_MIX,
    GT_MOTION_HOLD, GT_MOTION_ELASTIC, GT_MOTION_STUTTER, GT_MOTION_TAPESTOP,
    GT_MOTION_TUNNEL, GT_MOTION_PROBABILITY, GT_MOTION_LFO_DEPTH, GT_MOTION_SPIN, GT_MOTION_MIX,
    GT_COUNT
};
enum GranSource : int { GS_NONE = 0, GS_LFO, GS_RANDOM, GS_BUS, GS_COUNT };
enum GranLfoShape : int { GL_SIN = 0, GL_TRI, GL_SAW, GL_SQR, GL_COUNT };

// Buses: 0..7 = the sidechain of track 1..8 (its output peak), 8..15 = free "messages" other
// instruments / Vxx effects can write (engine side: AudioEngine::granBus).
constexpr int GRAN_BUS_COUNT = 16;
constexpr int GRAN_MODS      = 8;

// The engine's copy of the UI's GRAIN / ROAM / MOTION blocks. All bytes are 00-FF, like the cells.
struct GranularSettings {
    bool master = true;      // A/B switch: false = the instrument plays its sample normally (no cloud)
    struct Grain {
        bool state     = true;
        int  position  = 0x00;   // read head, 0..255 of the sample
        int  size      = 0x40;   // grain length, mapped to 1..1000 ms
        int  density   = 0x80;   // grains per second, mapped to 1..200
        int  spray     = 0x00;   // random spread around the head
        int  pitch     = 0x80;   // 0x80 = unison, +-24 semitones
        int  time      = 0x00;   // 0 = free (HZ), 1 = BPM (density follows density_div)
        int  density_div = 6;    // index of GRAN_BEATS (1/16 by default)
        int  window    = 0;      // HANN / TRI / RECT / GAUSS / EXPO
        int  spread    = 0x00;   // L/R read offset: decorrelates the channels
        int  attack    = 0x00;   // extra fade-in  of every grain (0..50 % of its length)
        int  decay     = 0x00;   // extra fade-out of every grain (0..50 % of its length)
        int  pan       = 0x00;   // random pan per grain
        int  jitter    = 0x00;   // random pitch per grain (up to +-12 semitones)
        // STRETCH: how much the cloud's read head advances by itself, 0 = frozen texture, 0xFF = the
        // full ratio. `ratio` is the target length / source length, encoded 0.25..4.0 (see stretchRatio()).
        int  stretch   = 0x00;
        int  ratio     = 0x80;
        int  mix       = 0xFF;   // 0xFF = the cloud alone (v1), lower = the normal sample + the cloud, 0x00 = the sample alone
        // Per-cell bypass (INSTRUMENT page OFF switch): the engine substitutes a neutral value.
        bool bypass_size     = false;
        bool bypass_density = false;
        bool bypass_pitch    = false;
        bool bypass_position= false;
    } grain;
    struct Roam {
        bool state     = false;
        int  mode      = 0;      // BOUNCE / LOOP / DRIFT / RANDOM
        int  range     = 0x00;   // how far the head travels, 0..100 % of the sample
        int  wander    = 0x00;   // extra random walk
        int  speed     = 0x80;   // ~0.05 .. 20 Hz (or bpm division when TIME = BPM)
        int  speed_div = 2;
        int  smooth    = 0x40;   // slew of the head
        int  freeze    = 0;      // 1 = the head stops where it is
        int  dcurve    = 0;      // LIN / EASE / EXP / SINE : shape of the BOUNCE sweep
        int  dir       = 0x00;   // >= 0x80 : the sweep runs backwards
        int  curve     = 0x00;   // reserved
        int  bpm       = 0x80;   // reserved
        int  drift     = 0x00;   // constant creep of the head
        int  step_rate = 0x00;   // >0 : the head moves in quantised steps
        int  inertia   = 0x00;   // extra slew
        int  target    = 0x00;   // a place the head is pulled to...
        int  tlock     = 0x00;   // ...by this much
        int  mix       = 0xFF;   // 0 = no ROAM offset, 0xFF = full ROAM offset

    } roam;
    struct Motion {
        bool state     = false;
        int  jump      = 0x00;   // probability a grain lands anywhere
        int  hold      = 0x00;   // probability a grain repeats the previous position
        int  rev       = 0x00;   // probability a grain plays backwards
        int  elastic   = 0x00;   // per-grain pitch bend
        int  chaos     = 0x00;   // random size / pitch / position on every grain
        int  stutter   = 1;      // each position is played N times
        int  tapestop  = 0x00;   // the pitch slides down while the note sounds
        int  tunnel    = 0x00;   // grains up to 4x longer
        int  probability = 0x00; // probability a grain is skipped (gaps)
        int  lfo_depth = 0x00;   // slow sine on the position
        int  step_glide = 0x00;  // reserved
        int  trigger   = 0x00;   // reserved
        int  spin      = 0x00;   // the stereo image rotates
        int  factor    = 0x00;   // reserved
        int  mix          = 0xFF;   // 0 = no MOTION effects, 0xFF = full MOTION effects

        bool bypass_dir = false;
    } motion;
    // One modulation row. amount: 0x80 = none, 0xFF = +100 %, 0x00 = -100 % (inverted).
    struct Mod {
        int target = GT_NONE;
        int src    = GS_NONE;
        int amount = 0x80;
        int rate   = 0x80;       // LFO / RANDOM speed
        int shape  = GL_SIN;     // LFO shape
        int bus    = 0;          // GS_BUS : which bus
    } mods[GRAN_MODS];
};

// Note values of TIME = BPM, in quarter notes. Same order as ui/modules/granular_ui.h GRAN_DIVS.
inline constexpr float GRAN_BEATS[15] = {16.f, 8.f, 4.f, 2.f, 1.f, 0.5f, 0.25f, 0.125f, 0.0625f,
                                         0.6667f, 0.3333f, 0.1667f, 1.5f, 0.75f, 0.375f};

// STRETCH's ratio byte -> target length / source length. 0 = 0.25x (four times shorter),
// 0x80 ~= 1x, 0xFF = 4x (four times longer). Exponential so the middle is unison.
inline float gran_stretch_ratio(int byte) {
    return 0.25f * std::pow(16.0f, (float)std::clamp(byte, 0, 255) / 255.0f);
}

// Grain windows, built once at program start (no work on the audio thread).
struct GrainWindowTable {
    static constexpr int N = 512;
    float t[5][N + 1];
    GrainWindowTable() {
        const double TAU = 6.283185307179586;
        for (int s = 0; s < 5; ++s)
            for (int i = 0; i <= N; ++i) {
                const double x = (double)i / N;
                double w = 0.0;
                switch (s) {
                    case 0: w = 0.5 - 0.5 * std::cos(TAU * x); break;                       // HANN
                    case 1: w = 1.0 - std::fabs(2.0 * x - 1.0); break;                       // TRI
                    case 2: w = std::fmin(1.0, std::fmin(x / 0.05, (1.0 - x) / 0.05)); break; // RECT
                    case 3: { const double d = (x - 0.5) / 0.17; w = std::exp(-0.5 * d * d); } break; // GAUSS
                    default: w = std::fmin(1.0, x / 0.02) * std::exp(-5.0 * x) * 1.15; break;          // EXPO
                }
                t[s][i] = (float)std::fmax(0.0, w);
            }
    }
};
inline const GrainWindowTable GRAIN_WINDOWS{};

inline float grain_window(int shape, float x) {
    const int   s = shape < 0 ? 0 : (shape > 4 ? 4 : shape);
    const float f = (x < 0.f ? 0.f : (x > 1.f ? 1.f : x)) * GrainWindowTable::N;
    const int   i = (int)f;
    const float a = GRAIN_WINDOWS.t[s][i];
    const float b = GRAIN_WINDOWS.t[s][i < GrainWindowTable::N ? i + 1 : i];
    return a + (b - a) * (f - (float)i);
}

// One windowed grain. `pos` walks the sample at `step` frames per output sample.
struct GranularGrain {
    bool   on    = false;
    double pos   = 0.0;
    double step  = 1.0;
    double dstep = 0.0;     // ELASTIC: step change per sample
    double offR  = 0.0;     // SPREAD: the right channel's read offset, in frames
    int    age   = 0;
    int    life  = 1;
    int    win   = 0;
    float  att   = 0.0f, dec = 0.0f;   // extra fades, as a fraction of `life`
    float  gL    = 1.0f, gR = 1.0f;    // pan gains
};

class GranularVoice {
public:
    static constexpr int MAX_GRAINS = 48;
    static constexpr int CONTROL_EVERY = 16;   // samples between two updates of roam + modulation

    void reset() {
        for (GranularGrain& g : grains_) g.on = false;
        running_ = false;
        enabled_ = false;
        timer_   = 0.0;
        rng_     = 0x9E3779B9u;
        startOverride_ = -1;
        resetMotion();
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
        timer_ = 0.0;   // the first grain lands on the next frame; the ramps were just reset
    }

    // Once per piece, from the published InstrumentParams. `sounding` says whether the ramps glide
    // (a note already running) or land on the value (the first piece). `startOverride` (0..255, or
    // -1) overrides POSITION when a slice is selected. `bpm` feeds TIME = BPM; `bus` = GRAN_BUS_COUNT
    // floats (sidechain / messages), may be null.
    void setParams(const GranularSettings& p, int length, float noteRate, bool sounding, int frames,
                   float sampleRate, int startOverride = -1, float bpm = 0.0f, const float* bus = nullptr) {
        if (!running_) return;
        cfg_        = p;
        // Per-cell bypass: substitute the neutral value so the sound ignores the cell.
        if (cfg_.grain.bypass_size)      cfg_.grain.size     = 0x40;
        if (cfg_.grain.bypass_density)   cfg_.grain.density  = 0x80;
        if (cfg_.grain.bypass_pitch)     cfg_.grain.pitch    = 0x80;
        if (cfg_.grain.bypass_position)  cfg_.grain.position = 0x00;
        if (cfg_.motion.bypass_dir)      cfg_.motion.rev     = 0x00;
        bpm_        = bpm;
        bus_        = bus;
        length_     = std::max(length, 2);
        sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
        noteRate_   = noteRate > 0.0f ? noteRate : 1.0f;
        enabled_    = (p.grain.state || p.roam.state || p.motion.state) && p.master;

        if (startOverride >= 0) startOverride_ = startOverride;
        const float pos = (startOverride_ >= 0)
            ? (float)std::clamp(startOverride_, 0, 255) / 255.0f
            : (float)std::clamp(cfg_.grain.position, 0, 255) / 255.0f;
        position_.set(pos, sounding, frames);
        size_.set((float)std::clamp(cfg_.grain.size, 0, 255), sounding, frames);
        density_.set((float)std::clamp(cfg_.grain.density, 0, 255), sounding, frames);
        spray_.set((float)std::clamp(p.grain.spray, 0, 255) / 255.0f, sounding, frames);
        pitch_.set((float)std::clamp(cfg_.grain.pitch, 0, 255), sounding, frames);
        mix_.set((float)std::clamp(p.grain.mix, 0, 255) / 255.0f, sounding, frames);
        // Mixed with the sample, the cloud is brought to about the sample's own level (a dense cloud of
        // long grains is several times louder than its source). Cloud alone (mix 0xFF) is left as it was.
        const float ms   = 1.0f + (float)std::clamp(cfg_.grain.size, 0, 255) * (999.0f / 255.0f);
        const float dens = 1.0f + (float)std::clamp(cfg_.grain.density, 0, 255) * (199.0f / 255.0f);
        const float overlap = ms * 0.001f * dens;
        const float comp = 1.0f / std::sqrt(std::max(1.0f, overlap * 0.375f));
        comp_.set(comp, sounding, frames);
    }

    // One output frame. The cloud sums every live grain into `outL`/`outR`.
    void render(const float* left, const float* right, float& outL, float& outR) {
        outL = 0.0f;
        outR = 0.0f;
        dry_ = 1.0f;
        if (!running_ || !enabled_ || !left || length_ < 2) return;
        frame(left, right, true, outL, outR);
    }

    // The same cloud, run without any audio: grains spawn, age and move exactly as they do when they
    // sound. The editor uses it to animate the waveform (it reads `collectView`). `n` output frames.
    void advance(int n) {
        if (!running_ || !enabled_ || length_ < 2) return;
        float a, b;
        for (int i = 0; i < n; ++i) frame(nullptr, nullptr, false, a, b);
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

    bool  running() const { return running_; }

    // What the editor draws: one entry per sounding grain.
    struct GrainView {
        float pos  = 0.0f;   // 0..1 of the sample
        float age  = 0.0f;   // 0..1 through the grain
        float span = 0.0f;   // how much of the sample the whole grain crosses (0..1), negative = backwards
        float win  = 0.0f;   // its window gain right now, 0..1
        float pan  = 0.0f;   // -1 (L) .. 1 (R)
        float semis = 0.0f;  // its pitch against the note, in semitones
    };
    int collectView(GrainView* out, int max) const {
        int n = 0;
        for (const GranularGrain& g : grains_) {
            if (!g.on || n >= max) continue;
            const float len = (float)std::max(length_, 1);
            GrainView& v = out[n++];
            v.pos  = std::clamp((float)(g.pos / (double)len), 0.0f, 1.0f);
            v.age  = (float)g.age / (float)std::max(g.life, 1);
            v.span = (float)(g.step * (double)g.life / (double)len);
            float w = grain_window(g.win, v.age);
            if (g.att > 0.0f) w *= std::min(1.0f, v.age / g.att);
            if (g.dec > 0.0f) w *= std::min(1.0f, (1.0f - v.age) / g.dec);
            v.win  = std::clamp(w, 0.0f, 1.0f);
            const float tot = g.gL + g.gR;
            v.pan  = tot > 0.0f ? (g.gR - g.gL) / tot : 0.0f;
            v.semis = 12.0f * std::log2(std::max(1e-4f, (float)(std::fabs(g.step) / (double)std::max(noteRate_, 1e-4f))));
        }
        return n;
    }
    // Where the read head is, 0..1 of the window: POSITION + ROAM + the modulation on POSITION,
    // plus the automatic advance of STRETCH.
    float headFraction() const {
        float p = position_.value() + mod_[GT_POSITION] + roamSmooth_ + (float)headPhase_;
        return p - std::floor(p);
    }
    int  windowStart() const { return winOn_ ? winStart_ : 0; }
    int  windowEnd()   const { return winOn_ ? std::min(winEnd_, length_ - 1) : length_ - 1; }
    // The cloud is the sound source right now (master AND GRAIN STATE). When false the voice must use
    // its classic playhead -- engine-mix.cpp asks this, not running().
    bool  active() const { return running_ && enabled_; }
    // MIX < 0xFF: the sample plays too. `blending()` = the engine must also run the classic playhead;
    // `dryGain()` = the share of the classic read at the sample just rendered (the cloud is already scaled).
    bool  blending() const { return mix_.goal < 0.999f || mix_.moving() || mod_[GT_GRAIN_MIX] < -0.001f; }
    float dryGain() const { return dry_; }

    // The frames the cloud lives in: the note's window (slice, CUT, or the instrument's START/END).
    // POSITION and SPRAY are fractions of it, and a grain that reaches its edge wraps inside it.
    // Fewer than 4 frames, or never called = the whole sample (the v1 behaviour).
    void setWindow(int startFrame, int endFrame) {
        if (endFrame - startFrame >= 4) { winStart_ = std::max(0, startFrame); winEnd_ = std::max(winStart_ + 4, endFrame); winOn_ = true; }
        else winOn_ = false;
    }
    // For the editor / popup: where the head is (0..1) and what each target is being pushed by.
    float roamOffset() const { return roamSmooth_; }
    float modValue(int target) const { return (target > 0 && target < GT_COUNT) ? mod_[target] : 0.0f; }

private:
    // One frame of the cloud. `read` false = no audio is touched (see advance()).
    void frame(const float* left, const float* right, bool read, float& outL, float& outR) {
        if ((ctl_++ & (CONTROL_EVERY - 1)) == 0) updateControl((float)CONTROL_EVERY / sampleRate_);
        const float mix = std::clamp(mix_.next() + mod_[GT_GRAIN_MIX], 0.0f, 1.0f);
        constexpr float HALF_PI = 1.57079632679f;
        dry_ = cosf(mix * HALF_PI);
        const float wet = sinf(mix * HALF_PI);
        const float cloudComp = 1.0f + (comp_.next() - 1.0f) * (1.0f - mix);

        const float densByte = std::clamp(density_.next() + mod_[GT_DENSITY] * 255.0f, 0.0f, 255.0f);
        float density = 1.0f + densByte * (199.0f / 255.0f);
        if (cfg_.grain.time == 1 && bpm_ > 0.0f) {   // TIME = BPM: one grain per note division
            const int   d     = std::clamp(cfg_.grain.density_div, 0, 14);
            const float beats = GRAN_BEATS[d];
            density = std::clamp((bpm_ / 60.0f) / beats * (0.5f + densByte / 255.0f), 0.05f, 400.0f);
        }
        const double interval = (double)sampleRate_ / (double)std::max(density, 0.05f);
        timer_ -= 1.0;
        if (timer_ <= 0.0) {
            spawn();
            timer_ += interval;
            if (timer_ < 0.0) timer_ = 0.0;
        }

        float sumL = 0.0f, sumR = 0.0f;
        const float* rsrc = right ? right : left;
        for (GranularGrain& g : grains_) {
            if (!g.on) continue;
            const int idx = (int)g.pos;
            if (idx < 0 || idx >= length_ - 1) { g.on = false; continue; }
            if (read) {
                const float frac = (float)(g.pos - (double)idx);
                const float x    = (float)g.age / (float)g.life;
                float w = grain_window(g.win, x);
                if (g.att > 0.0f) w *= std::min(1.0f, x / g.att);
                if (g.dec > 0.0f) w *= std::min(1.0f, (1.0f - x) / g.dec);
                const float l = left[idx] + (left[idx + 1] - left[idx]) * frac;
                float r;
                if (g.offR != 0.0) {
                    const double q = std::clamp(g.pos + g.offR, 0.0, (double)(length_ - 2));
                    const int    j = (int)q;
                    const float  f = (float)(q - (double)j);
                    r = rsrc[j] + (rsrc[j + 1] - rsrc[j]) * f;
                } else {
                    r = right ? (right[idx] + (right[idx + 1] - right[idx]) * frac) : l;
                }
                sumL += l * w * g.gL;
                sumR += r * w * g.gR;
            }
            g.pos  += g.step;
            g.step += g.dstep;
            if (++g.age >= g.life) g.on = false;
        }
        const float lvl = std::max(0.0f, 1.0f + mod_[GT_LEVEL]) * wet * cloudComp;
        outL = sumL * lvl;
        outR = sumR * lvl;
    }

    uint32_t nextRandom() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }
    float rnd()  { return (float)(nextRandom() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float rndb() { return rnd() * 2.0f - 1.0f; }                                   // -1..1

    void resetMotion() {
        ctl_ = 0;
        for (float& m : mod_) m = 0.0f;
        for (int i = 0; i < GRAN_MODS; ++i) { lfoPh_[i] = 0.0f; shVal_[i] = 0.0f; }
        roamPh_ = 0.0f; roamTarget_ = 0.0f; walk_ = 0.0f; walk2_ = 0.0f; driftAcc_ = 0.0f;
        roamSmooth_ = 0.0f; posLfo_ = 0.0f; spinPh_ = 0.0f; tapeT_ = 0.0f; tapeMul_ = 1.0f;
        headPhase_ = 0.0;
        haveLast_ = false; lastStart_ = 0.0; stutterLeft_ = 0;
    }

    static float shapeCurve(int c, float x) {   // x 0..1 -> 0..1
        switch (c) {
            case 1:  return x * x * (3.0f - 2.0f * x);
            case 2:  return x * x;
            case 3:  return 0.5f - 0.5f * cosf(3.14159265f * x);
            default: return x;
        }
    }
    float lfoWave(int shape, float ph) const {   // -1..1
        switch (shape) {
            case GL_TRI: return 1.0f - 4.0f * std::fabs(ph - 0.5f) ;
            case GL_SAW: return 2.0f * ph - 1.0f;
            case GL_SQR: return ph < 0.5f ? 1.0f : -1.0f;
            default:     return sinf(6.28318530718f * ph);
        }
    }

    // Roam + modulation, every CONTROL_EVERY samples.
    void updateControl(float dt) {
        // ---- modulation matrix --------------------------------------------------------------
        for (float& m : mod_) m = 0.0f;
        for (int i = 0; i < GRAN_MODS; ++i) {
            const auto& md = cfg_.mods[i];
            if (md.target <= GT_NONE || md.target >= GT_COUNT || md.src <= GS_NONE) continue;
            const float amt = ((float)std::clamp(md.amount, 0, 255) - 128.0f) / 127.0f;   // -1..1
            if (amt == 0.0f) continue;
            const float hz = 0.05f * powf(400.0f, (float)std::clamp(md.rate, 0, 255) / 255.0f);   // 0.05..20 Hz
            float v = 0.0f;
            if (md.src == GS_LFO) {
                lfoPh_[i] += hz * dt;
                lfoPh_[i] -= floorf(lfoPh_[i]);
                v = lfoWave(md.shape, lfoPh_[i]);
            } else if (md.src == GS_RANDOM) {
                const float before = lfoPh_[i];
                lfoPh_[i] += hz * dt;
                if (lfoPh_[i] >= 1.0f) { lfoPh_[i] -= floorf(lfoPh_[i]); shVal_[i] = rndb(); }
                (void)before;
                v = shVal_[i];
            } else if (md.src == GS_BUS && bus_) {
                v = std::clamp(bus_[std::clamp(md.bus, 0, GRAN_BUS_COUNT - 1)], 0.0f, 1.0f) * 2.0f - 1.0f;
                v = (v + 1.0f) * 0.5f;   // a bus is 0..1: the amount scales it (sidechain = 0 when quiet)
            }
            mod_[md.target] += v * amt;
        }

        // ---- ROAM: the head's offset --------------------------------------------------------
        const auto& rm = cfg_.roam;
        float off = 0.0f;
        if (rm.state) {
            float hz = 0.05f * powf(400.0f, (float)std::clamp(rm.speed, 0, 255) / 255.0f);
            if (cfg_.grain.time == 1 && bpm_ > 0.0f)
                hz = (bpm_ / 60.0f) / GRAN_BEATS[std::clamp(rm.speed_div, 0, 14)];
            hz *= 1.0f + mod_[GT_ROAM_SPEED] * 2.0f;
            hz = std::max(0.0f, hz);
            const float range = std::clamp((float)rm.range / 255.0f + mod_[GT_ROAM_RANGE], 0.0f, 1.0f);
            const float wander = std::clamp((float)rm.wander / 255.0f + mod_[GT_ROAM_WANDER], 0.0f, 1.0f);
            if (rm.freeze == 0) {
                const float dir = rm.dir >= 0x80 ? -1.0f : 1.0f;
                const float prev = roamPh_;
                roamPh_ += dir * hz * dt;
                roamPh_ -= floorf(roamPh_);
                if (rm.mode == 3 && ((dir > 0 && roamPh_ < prev) || (dir < 0 && roamPh_ > prev))) roamTarget_ = rnd();
                if (rm.mode == 2) walk_ = std::clamp(walk_ + rndb() * sqrtf(hz * dt) * 0.8f, -1.0f, 1.0f);
                driftAcc_ += (float)rm.drift / 255.0f * 0.05f * dt;
                driftAcc_ -= floorf(driftAcc_);
            }
            switch (rm.mode) {
                case 0: off = shapeCurve(rm.dcurve, 1.0f - std::fabs(2.0f * roamPh_ - 1.0f)) * range; break;   // BOUNCE
                case 1: off = roamPh_ * range; break;                                                            // LOOP
                case 2: off = walk_ * range; break;                                                              // DRIFT
                case 4: off = 0.0f; break;                                                                       // SLC: head locked to the slice start
                default: off = roamTarget_ * range; break;                                                       // RANDOM
            }
            if (wander > 0.0f && rm.freeze == 0)
                walk2_ = std::clamp(walk2_ + rndb() * sqrtf(dt) * wander * 2.0f, -1.0f, 1.0f);
            off += walk2_ * wander * 0.5f + driftAcc_;
            const float stepRate = std::clamp((float)rm.step_rate / 255.0f + mod_[GT_ROAM_STEP_RATE], 0.0f, 1.0f);
            if (stepRate > 0.0f) {
                const float q = 2.0f + stepRate * 255.0f / 8.0f;
                off = floorf(off * q) / q;
            }
        }
        const float roamMix = std::clamp((float)rm.mix / 255.0f + mod_[GT_ROAM_MIX], 0.0f, 1.0f);
        off *= roamMix;
        const float smooth = std::clamp((float)rm.smooth / 255.0f + mod_[GT_ROAM_SMOOTH], 0.0f, 1.0f);
        const float inertia = std::clamp((float)rm.inertia / 255.0f + mod_[GT_ROAM_INERTIA], 0.0f, 1.0f);
        const float tau = smooth * 0.5f + inertia * 1.0f;
        if (tau > 0.0005f) roamSmooth_ += (off - roamSmooth_) * (1.0f - expf(-dt / tau));
        else roamSmooth_ = off;

        // ---- STRETCH: the head advances by itself, at a constant pitch ----------------------
        // The grains keep their own step (noteRate), so slowing the head stretches the sample in
        // time without lowering it. 0 = the frozen texture of v1; 0xFF = the full DURATION/NEW BPM
        // ratio. The LFO on GT_STRETCH rides on the same value, hence the clamp.
        const float stretchAmt =
            std::clamp((float)std::clamp(cfg_.grain.stretch, 0, 255) / 255.0f + mod_[GT_STRETCH],
                       0.0f, 1.0f);
        if (stretchAmt > 0.0f) {
            const double spanFrames =
                winOn_ ? (double)std::max(1, std::min(winEnd_, length_ - 1) - winStart_)
                       : (double)std::max(1, length_ - 1);
            const double windowSec = spanFrames / (double)std::max(1.0f, sampleRate_);
            const double ratio     = (double)gran_stretch_ratio(cfg_.grain.ratio);
            const double speed     = 1.0 / std::max(0.001, ratio * windowSec);   // fraction / second
            headPhase_ += speed * (double)stretchAmt * (double)dt;
            headPhase_ -= std::floor(headPhase_);
        }

        // ---- MOTION: things that evolve while the note sounds -------------------------------
        const auto& mo = cfg_.motion;
        const float motionMix = mo.state ? std::clamp((float)mo.mix / 255.0f + mod_[GT_MOTION_MIX], 0.0f, 1.0f) : 0.0f;
        const float motionModMix = mo.state ? motionMix : 1.0f;
        if (mo.state) {
            const float lfoDepth = std::clamp((float)mo.lfo_depth / 255.0f * motionMix + mod_[GT_MOTION_LFO_DEPTH] * motionModMix, 0.0f, 1.0f);
            if (lfoDepth > 0.0f) {
                posLfo_ += (0.05f * powf(400.0f, (float)std::clamp(rm.speed, 0, 255) / 255.0f)) * dt;
                posLfo_ -= floorf(posLfo_);
            }
            const float spin = std::clamp((float)mo.spin / 255.0f * motionMix + mod_[GT_MOTION_SPIN] * motionModMix, 0.0f, 1.0f);
            spinPh_ += spin * 2.0f * dt;
            spinPh_ -= floorf(spinPh_);
            const float tapestop = std::clamp((float)mo.tapestop / 255.0f * motionMix + mod_[GT_MOTION_TAPESTOP] * motionModMix, 0.0f, 1.0f);
            if (tapestop > 0.0f) {
                tapeT_ += dt;
                tapeMul_ = std::max(0.02f, expf(-tapeT_ * tapestop * 3.0f));
            } else {
                tapeT_ = 0.0f;
                tapeMul_ = 1.0f;
            }
        } else {
            tapeT_ = 0.0f;
            tapeMul_ = 1.0f;
        }
    }

    void spawn() {
        GranularGrain* dst = nullptr;
        for (GranularGrain& g : grains_) {
            if (!g.on) { dst = &g; break; }
        }
        if (!dst) {   // the oldest one is nearest its end: quietest
            dst = &grains_[0];
            for (GranularGrain& g : grains_) if (g.age > dst->age) dst = &g;
        }

        const auto& gr = cfg_.grain;
        const auto& mo = cfg_.motion;
        const bool  mv = mo.state;
        const float motionMix = mv ? std::clamp((float)mo.mix / 255.0f + mod_[GT_MOTION_MIX], 0.0f, 1.0f) : 0.0f;
        const float modMix = mv ? motionMix : 1.0f;
        const float probability = std::clamp((float)mo.probability / 255.0f * motionMix + mod_[GT_MOTION_PROBABILITY] * modMix, 0.0f, 1.0f);
        const float hold = std::clamp((float)mo.hold / 255.0f * motionMix + mod_[GT_MOTION_HOLD] * modMix, 0.0f, 1.0f);
        const float tunnel = std::clamp((float)mo.tunnel / 255.0f * motionMix + mod_[GT_MOTION_TUNNEL] * modMix, 0.0f, 1.0f);
        const float elastic = std::clamp((float)mo.elastic / 255.0f * motionMix + mod_[GT_MOTION_ELASTIC] * modMix, 0.0f, 1.0f);
        const float stutter = std::clamp(1.0f + (float)(mo.stutter - 1) * motionMix + mod_[GT_MOTION_STUTTER] * 15.0f * modMix, 1.0f, 16.0f);
        const int stutterCount = (int)std::lround(stutter);

        // MOTION: gaps
        if (mv && probability > 0.0f && rnd() < probability) return;

        const float chaos = mv ? std::clamp((float)mo.chaos / 255.0f * motionMix + mod_[GT_CHAOS] * modMix, 0.0f, 1.0f)
                               : std::clamp(mod_[GT_CHAOS], 0.0f, 1.0f);

        // ---- position -----------------------------------------------------------------------
        float basePos = position_.next() + mod_[GT_POSITION] + roamSmooth_ + (float)headPhase_;
        const float lfoDepth = std::clamp((float)mo.lfo_depth / 255.0f * motionMix + mod_[GT_MOTION_LFO_DEPTH] * modMix, 0.0f, 1.0f);
        if (mv && lfoDepth > 0.0f) basePos += sinf(6.28318530718f * posLfo_) * lfoDepth * 0.25f;
        if (cfg_.roam.state) {
            const float roamMix = std::clamp((float)cfg_.roam.mix / 255.0f + mod_[GT_ROAM_MIX], 0.0f, 1.0f);
            const float target = std::clamp((float)cfg_.roam.target / 255.0f + mod_[GT_ROAM_TARGET], 0.0f, 1.0f);
            const float lock = std::clamp((float)cfg_.roam.tlock / 255.0f + mod_[GT_ROAM_TLOCK], 0.0f, 1.0f);
            const float k = lock * roamMix;
            basePos = basePos + (target - basePos) * k;
        }
        const float spray = std::clamp(spray_.next() + mod_[GT_SPRAY], 0.0f, 1.0f) * 0.5f;
        const float spread = std::clamp((float)gr.spread / 255.0f + mod_[GT_GRAIN_SPREAD], 0.0f, 1.0f);
        const double wLo = winOn_ ? (double)winStart_ : 0.0;
        const double wHi = winOn_ ? (double)std::min(winEnd_, length_ - 1) : (double)(length_ - 1);
        const double wSpan = std::max(2.0, wHi - wLo);
        double start = wLo + (double)basePos * (wSpan - 1.0);
        if (spray > 0.0f) start += (double)rndb() * (double)spray * wSpan;
        if (chaos > 0.0f) start += (double)rndb() * (double)chaos * 0.25 * wSpan;
        start = std::clamp(start, 0.0, (double)(length_ - 1));

        const float pJump = mv ? std::clamp((float)mo.jump / 255.0f * motionMix + mod_[GT_JUMP] * modMix, 0.0f, 1.0f)
                               : std::clamp(mod_[GT_JUMP], 0.0f, 1.0f);
        if (pJump > 0.0f && rnd() < pJump) start = wLo + (double)rnd() * (wSpan - 1.0);

        // HOLD / STUTTER: replay the previous position
        if (mv && haveLast_) {
            if (stutterLeft_ > 0) { start = lastStart_; --stutterLeft_; }
            else if (hold > 0.0f && rnd() < hold) start = lastStart_;
            else if (stutterCount > 1) stutterLeft_ = stutterCount - 1;
        } else if (mv && stutterCount > 1) {
            stutterLeft_ = stutterCount - 1;
        }
        start = std::clamp(start, wLo, std::max(wLo, wHi - 1.0));
        lastStart_ = start;
        haveLast_  = true;

        // ---- length -------------------------------------------------------------------------
        float sizeByte = std::clamp(size_.next() + mod_[GT_SIZE] * 255.0f, 0.0f, 255.0f);
        float lifeMs   = 1.0f + sizeByte * (999.0f / 255.0f);
        if (mv && tunnel > 0.0f) lifeMs *= 1.0f + tunnel * 3.0f;
        if (chaos > 0.0f) lifeMs *= std::max(0.2f, 1.0f + rndb() * chaos * 0.7f);
        const int life = std::max(2, (int)(lifeMs * sampleRate_ / 1000.0f));

        // ---- pitch --------------------------------------------------------------------------
        float semis = ((std::clamp(pitch_.next() + mod_[GT_PITCH] * 255.0f, 0.0f, 255.0f) - 128.0f) / 128.0f) * 24.0f;
        const float jit = std::clamp((float)gr.jitter / 255.0f + mod_[GT_JITTER], 0.0f, 1.0f);
        if (jit > 0.0f)   semis += rndb() * 12.0f * jit;
        if (chaos > 0.0f) semis += rndb() * 7.0f * chaos;
        double step = (double)noteRate_ * (double)powf(2.0f, semis / 12.0f) * (double)tapeMul_;

        // ---- direction + elastic ------------------------------------------------------------
        const float pRev = mv ? std::clamp((float)mo.rev / 255.0f * motionMix + mod_[GT_REV] * modMix, 0.0f, 1.0f)
                              : std::clamp(mod_[GT_REV], 0.0f, 1.0f);
        if (pRev > 0.0f && rnd() < pRev) step = -step;
        double dstep = 0.0;
        if (mv && mo.elastic > 0) {
            const float bend = rndb() * elastic * 0.5f;   // octaves over the grain
            dstep = step * (double)(powf(2.0f, bend) - 1.0f) / (double)life;
        }

        // ---- stereo -------------------------------------------------------------------------
        float p = rndb() * std::clamp((float)gr.pan / 255.0f + mod_[GT_STEREO], 0.0f, 1.0f);
        const float spin = std::clamp((float)mo.spin / 255.0f * motionMix + mod_[GT_MOTION_SPIN] * modMix, 0.0f, 1.0f);
        if (mv && spin > 0.0f) p += sinf(6.28318530718f * spinPh_) * spin;
        p = std::clamp(p, -1.0f, 1.0f);
        const float ang = (p + 1.0f) * 0.7853981634f;

        dst->on    = true;
        dst->pos   = start;
        dst->step  = step;
        dst->dstep = dstep;
        dst->offR  = spread > 0.0f ? (double)rndb() * (double)spread * 0.02 * (double)length_ : 0.0;
        dst->age   = 0;
        dst->life  = life;
        dst->win   = std::clamp((int)std::lround((float)gr.window + mod_[GT_GRAIN_WINDOW] * 4.0f), 0, 4);
        dst->att   = std::clamp((float)gr.attack / 255.0f + mod_[GT_GRAIN_ATTACK], 0.0f, 1.0f) * 0.5f;
        dst->dec   = std::clamp((float)gr.decay / 255.0f + mod_[GT_GRAIN_DECAY], 0.0f, 1.0f) * 0.5f;
        dst->gL    = cosf(ang) * 1.41421356f;
        dst->gR    = sinf(ang) * 1.41421356f;
    }

    GranularGrain grains_[MAX_GRAINS];
    GranularSettings cfg_{};
    const float* bus_   = nullptr;
    float   bpm_        = 0.0f;
    int     length_     = 0;
    float   sampleRate_ = 48000.0f;
    float   noteRate_   = 1.0f;
    bool    running_    = false;
    bool    enabled_    = false;
    double  timer_      = 0.0;
    uint32_t rng_       = 0x9E3779B9u;
    int      startOverride_ = -1;
    uint32_t ctl_       = 0;
    float    mod_[GT_COUNT] = {};
    float    lfoPh_[GRAN_MODS] = {}, shVal_[GRAN_MODS] = {};
    float    roamPh_ = 0, roamTarget_ = 0, walk_ = 0, walk2_ = 0, driftAcc_ = 0, roamSmooth_ = 0;
    float    posLfo_ = 0, spinPh_ = 0, tapeT_ = 0, tapeMul_ = 1;
    double   headPhase_ = 0.0;   // STRETCH: the head's automatic advance, 0..1 of the window
    bool     haveLast_ = false;
    double   lastStart_ = 0.0;
    int      stutterLeft_ = 0;
    int      winStart_ = 0, winEnd_ = 0;
    bool     winOn_ = false;
    Ramp    position_, size_, density_, spray_, pitch_, mix_, comp_;
    float   dry_ = 1.0f;
};

}  // namespace pt
