// grain_preview.h -- MOONSET : simulation du nuage granulaire pour animer les traits rouges.
// Rejoue, cote UI, la meme logique que granular-voice.h (updateControl + spawn) : ROAM, MOTION et la
// matrice de modulation (LFO / RANDOM) font bouger les grains meme quand rien ne joue. Quand le moteur
// joue vraiment, le dessin prend ses positions reelles (grainHeads) et ce simulateur sert de repli.
// Aucune allocation, aucun lien avec le thread audio. Positions : 0..1 de l'echantillon entier.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "granular/granular-voice.h"
#include "songcore/model.h"

namespace pt::ui {

struct GrainPreview {
    using Params = songcore::Instrument::GranularParams;

    struct G {
        bool  on   = false;
        float pos  = 0.0f;    // 0..1
        float step = 0.0f;    // 0..1 par seconde, signe = sens de lecture
        float age  = 0.0f;    // s
        float life = 0.05f;   // s
        int   win  = 0;
        bool  rev  = false;
    };
    static constexpr int MAX = 48;
    G g[MAX];

    // ---- Sorties lues par le dessin ------------------------------------------------------------
    float head       = 0.0f;   // tete de lecture : POS + ROAM + LFO de position, dans la tranche
    float anchor     = 0.0f;   // POS seule, sans le mouvement de ROAM
    float roamOff    = 0.0f;   // decalage courant de ROAM (0..1 de la tranche)
    float rangeEnd   = 0.0f;   // fin de la plage de ROAM
    float sprayHalf  = 0.0f;   // demi-largeur de dispersion (0..1 de l'echantillon)
    float chaosHalf  = 0.0f;   // idem pour CHAOS
    float densHz     = 1.0f;
    float sizeMs     = 1.0f;
    float semis      = 0.0f;   // hauteur nominale en demi-tons
    float dryPos     = 0.0f;   // tete du sample sec (mix < FF)
    int   count      = 0;      // grains vivants
    float lastSkipAt = 0.0f;   // ou le dernier grain saute (trou) aurait du tomber
    long long lastSkipMs = -1000000;

    void reset() {
        for (G& x : g) x.on = false;
        timer_ = 0.0f; roamPh_ = 0.0f; roamTarget_ = 0.0f; walk_ = walk2_ = driftAcc_ = roamSmooth_ = 0.0f;
        posLfo_ = spinPh_ = tapeT_ = 0.0f; tapeMul_ = 1.0f;
        for (int i = 0; i < 8; ++i) { lfoPh_[i] = 0.0f; shVal_[i] = 0.0f; }
        haveLast_ = false; stutterLeft_ = 0; dryPos = 0.0f;
        if (rng_ == 0) rng_ = 0x9E3779B9u;
    }

    // lo..hi : la tranche lue (0..1 de l'echantillon), sliced = true si une tranche est active.
    void advance(const Params& p, long long nowMs, float lo, float hi, bool sliced,
                 float sampleSec, float bpm, const float* bus = nullptr) {
        if (lastMs_ < 0 || nowMs - lastMs_ > 500) reset();
        float dt = lastMs_ < 0 ? 0.0f : std::clamp((float)(nowMs - lastMs_) * 0.001f, 0.0f, 0.1f);
        lastMs_ = nowMs;
        if (dt <= 0.0f) return;   // un second dessin dans la meme image : rien ne bouge

        lo_ = std::clamp(lo, 0.0f, 1.0f);
        hi_ = std::clamp(hi, lo_ + 0.0005f, 1.0f);
        sliced_ = sliced;
        sampleSec_ = std::max(sampleSec, 0.01f);
        updateControl(p, dt, bpm, bus);

        // Densite -> intervalle entre deux grains.
        const float densByte = std::clamp((float)p.grain.density + mod_[pt::GT_DENSITY] * 255.0f, 0.0f, 255.0f);
        float density = 1.0f + densByte * (199.0f / 255.0f);
        if (p.grain.time == 1 && bpm > 0.0f) {
            const float beats = pt::GRAN_BEATS[std::clamp(p.grain.density_div, 0, 14)];
            density = std::clamp((bpm / 60.0f) / beats * (0.5f + densByte / 255.0f), 0.05f, 400.0f);
        }
        densHz = density;
        timer_ -= dt;
        for (int guard = 0; timer_ <= 0.0f && guard < 8; ++guard) {
            spawn(p, nowMs);
            timer_ += 1.0f / std::max(density, 0.05f);
        }
        if (timer_ < 0.0f) timer_ = 0.0f;

        // Avance des grains : comme la voix, un grain boucle dans la tranche au lieu d'en sortir.
        count = 0;
        const float span = hi_ - lo_;
        for (G& x : g) {
            if (!x.on) continue;
            x.age += dt;
            x.pos += x.step * dt;
            if (x.age >= x.life) { x.on = false; continue; }
            if (sliced_ && span > 0.002f) {
                if (x.pos >= hi_) x.pos = lo_ + std::fmod(x.pos - lo_, span);
                else if (x.pos < lo_) x.pos = hi_ - std::fmod(lo_ - x.pos, span);
            } else if (x.pos < 0.0f || x.pos > 1.0f) { x.on = false; continue; }
            ++count;
        }

        // Le sample sec (MIX < FF) : tete a vitesse normale, qui boucle dans la tranche.
        dryPos += dt / sampleSec_;
        if (dryPos >= hi_ || dryPos < lo_) dryPos = lo_;
    }

    // 0..1 : gain de la fenetre au moment present du grain (sa hauteur a l'ecran).
    static float window_gain(const G& x) {
        return pt::grain_window(x.win, std::clamp(x.age / std::max(x.life, 1e-4f), 0.0f, 1.0f));
    }
    float modValue(int target) const {
        return target > 0 && target < pt::GT_COUNT ? mod_[target] : 0.0f;
    }
    // Ou le grain sera en fin de vie : la trace de sa course restante.
    static float end_pos(const G& x) { return x.pos + x.step * (x.life - x.age); }

private:
    float rnd()  { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (float)(rng_ >> 8) * (1.0f / 16777216.0f); }
    float rndb() { return rnd() * 2.0f - 1.0f; }

    static float shapeCurve(int c, float x) {
        switch (c) {
            case 1:  return x * x * (3.0f - 2.0f * x);
            case 2:  return x * x;
            case 3:  return 0.5f - 0.5f * std::cos(3.14159265f * x);
            default: return x;
        }
    }
    static float lfoWave(int shape, float ph) {
        switch (shape) {
            case pt::GL_TRI: return 1.0f - 4.0f * std::fabs(ph - 0.5f);
            case pt::GL_SAW: return 2.0f * ph - 1.0f;
            case pt::GL_SQR: return ph < 0.5f ? 1.0f : -1.0f;
            default:         return std::sin(6.28318530718f * ph);
        }
    }

    void updateControl(const Params& p, float dt, float bpm, const float* bus) {
        for (float& m : mod_) m = 0.0f;
        for (int i = 0; i < pt::GRAN_MODS; ++i) {
            const auto& md = p.mods[i];
            if (md.target <= pt::GT_NONE || md.target >= pt::GT_COUNT || md.src <= pt::GS_NONE) continue;
            const float amt = ((float)std::clamp(md.amount, 0, 255) - 128.0f) / 127.0f;
            if (amt == 0.0f) continue;
            const float hz = 0.05f * std::pow(400.0f, (float)std::clamp(md.rate, 0, 255) / 255.0f);
            float v = 0.0f;
            if (md.src == pt::GS_LFO) {
                lfoPh_[i] += hz * dt; lfoPh_[i] -= std::floor(lfoPh_[i]);
                v = lfoWave(md.shape, lfoPh_[i]);
            } else if (md.src == pt::GS_RANDOM) {
                lfoPh_[i] += hz * dt;
                if (lfoPh_[i] >= 1.0f) { lfoPh_[i] -= std::floor(lfoPh_[i]); shVal_[i] = rndb(); }
                v = shVal_[i];
            } else if (md.src == pt::GS_BUS && bus) {
                v = std::clamp(bus[std::clamp(md.bus, 0, pt::GRAN_BUS_COUNT - 1)], 0.0f, 1.0f);
            }
            mod_[md.target] += v * amt;
        }

        const auto& rm = p.roam;
        float off = 0.0f;
        float range = std::clamp((float)rm.range / 255.0f + mod_[pt::GT_ROAM_RANGE], 0.0f, 1.0f);
        const float wander = std::clamp((float)rm.wander / 255.0f + mod_[pt::GT_ROAM_WANDER], 0.0f, 1.0f);
        if (rm.state) {
            float hz = 0.05f * std::pow(400.0f, (float)std::clamp(rm.speed, 0, 255) / 255.0f);
            if (p.grain.time == 1 && bpm > 0.0f)
                hz = (bpm / 60.0f) / pt::GRAN_BEATS[std::clamp(rm.speed_div, 0, 14)];
            hz *= 1.0f + mod_[pt::GT_ROAM_SPEED] * 2.0f;
            hz = std::max(0.0f, hz);
            if (rm.freeze == 0) {
                const float dir = rm.dir >= 0x80 ? -1.0f : 1.0f;
                const float prev = roamPh_;
                roamPh_ += dir * hz * dt; roamPh_ -= std::floor(roamPh_);
                if (rm.mode == 3 && ((dir > 0 && roamPh_ < prev) || (dir < 0 && roamPh_ > prev))) roamTarget_ = rnd();
                if (rm.mode == 2) walk_ = std::clamp(walk_ + rndb() * std::sqrt(hz * dt) * 0.8f, -1.0f, 1.0f);
                driftAcc_ += (float)rm.drift / 255.0f * 0.05f * dt; driftAcc_ -= std::floor(driftAcc_);
            }
            switch (rm.mode) {
                case 0:  off = shapeCurve(rm.dcurve, 1.0f - std::fabs(2.0f * roamPh_ - 1.0f)) * range; break;
                case 1:  off = roamPh_ * range; break;
                case 2:  off = walk_ * range; break;
                default: off = roamTarget_ * range; break;
            }
            if (wander > 0.0f && rm.freeze == 0)
                walk2_ = std::clamp(walk2_ + rndb() * std::sqrt(dt) * wander * 2.0f, -1.0f, 1.0f);
            off += walk2_ * wander * 0.5f + driftAcc_;
            const float stepRate = std::clamp((float)rm.step_rate / 255.0f + mod_[pt::GT_ROAM_STEP_RATE], 0.0f, 1.0f);
            if (stepRate > 0.0f) {
                const float q = 2.0f + stepRate * 255.0f / 8.0f;
                off = std::floor(off * q) / q;
            }
        }
        const float roamMix = std::clamp((float)rm.mix / 255.0f + mod_[pt::GT_ROAM_MIX], 0.0f, 1.0f);
        off *= roamMix;
        const float smooth = std::clamp((float)rm.smooth / 255.0f + mod_[pt::GT_ROAM_SMOOTH], 0.0f, 1.0f);
        const float inertia = std::clamp((float)rm.inertia / 255.0f + mod_[pt::GT_ROAM_INERTIA], 0.0f, 1.0f);
        const float tau = smooth * 0.5f + inertia;
        if (tau > 0.0005f) roamSmooth_ += (off - roamSmooth_) * (1.0f - std::exp(-dt / tau));
        else roamSmooth_ = off;
        roamOff = roamSmooth_;

        const auto& mo = p.motion;
        const float motionMix = mo.state
                                    ? std::clamp((float)mo.mix / 255.0f + mod_[pt::GT_MOTION_MIX], 0.0f, 1.0f)
                                    : 0.0f;
        const float motionModMix = mo.state ? motionMix : 1.0f;
        const float lfoDepth = std::clamp((float)mo.lfo_depth / 255.0f * motionMix +
                                              mod_[pt::GT_MOTION_LFO_DEPTH] * motionModMix,
                                          0.0f, 1.0f);
        if (mo.state) {
            if (lfoDepth > 0.0f) {
                posLfo_ += 0.05f * std::pow(400.0f, (float)std::clamp(rm.speed, 0, 255) / 255.0f) * dt;
                posLfo_ -= std::floor(posLfo_);
            }
            const float spin = std::clamp((float)mo.spin / 255.0f * motionMix +
                                              mod_[pt::GT_MOTION_SPIN] * motionModMix,
                                          0.0f, 1.0f);
            spinPh_ += spin * 2.0f * dt; spinPh_ -= std::floor(spinPh_);
            const float tapestop = std::clamp((float)mo.tapestop / 255.0f * motionMix +
                                                  mod_[pt::GT_MOTION_TAPESTOP] * motionModMix,
                                              0.0f, 1.0f);
            if (tapestop > 0.0f) {
                tapeT_ += dt;
                tapeMul_ = std::max(0.02f, std::exp(-tapeT_ * tapestop * 3.0f));
            }
        } else { tapeT_ = 0.0f; tapeMul_ = 1.0f; }

        // Tete courante (sans dispersion) : ce que les traits montrent comme "la position".
        const float span = hi_ - lo_;
        float bp = (float)p.grain.position / 255.0f + mod_[pt::GT_POSITION];
        bp -= std::floor(bp);
        anchor = lo_ + bp * span;
        float hp = bp + roamSmooth_;
        if (mo.state && lfoDepth > 0.0f) hp += std::sin(6.28318530718f * posLfo_) * lfoDepth * 0.25f;
        if (rm.state) {
            const float target = std::clamp((float)rm.target / 255.0f + mod_[pt::GT_ROAM_TARGET], 0.0f, 1.0f);
            const float lock = std::clamp((float)rm.tlock / 255.0f + mod_[pt::GT_ROAM_TLOCK], 0.0f, 1.0f) * roamMix;
            hp += (target - hp) * lock;
        }
        hp -= std::floor(hp);
        head = lo_ + hp * span;
        rangeEnd = std::min(anchor + range * span, hi_);
        sprayHalf = std::clamp((float)p.grain.spray / 255.0f + mod_[pt::GT_SPRAY], 0.0f, 1.0f) * 0.5f * span;
        const float chaos = mo.state ? std::clamp((float)mo.chaos / 255.0f * motionMix +
                                  mod_[pt::GT_CHAOS] * motionModMix, 0.0f, 1.0f)
                                     : std::clamp(mod_[pt::GT_CHAOS], 0.0f, 1.0f);
        chaosHalf = chaos * 0.25f * span;
        sizeMs = 1.0f + std::clamp((float)p.grain.size + mod_[pt::GT_SIZE] * 255.0f, 0.0f, 255.0f) * (999.0f / 255.0f);
        semis = ((std::clamp((float)p.grain.pitch + mod_[pt::GT_PITCH] * 255.0f, 0.0f, 255.0f) - 128.0f) / 128.0f) * 24.0f;
    }

    void spawn(const Params& p, long long nowMs) {
        G* dst = nullptr;
        for (G& x : g) if (!x.on) { dst = &x; break; }
        if (!dst) { dst = &g[0]; for (G& x : g) if (x.age > dst->age) dst = &x; }

        const auto& gr = p.grain;
        const auto& mo = p.motion;
        const bool  mv = mo.state;
        const float motionMix = mv
                                    ? std::clamp((float)mo.mix / 255.0f + mod_[pt::GT_MOTION_MIX], 0.0f, 1.0f)
                                    : 0.0f;
        const float modMix = mv ? motionMix : 1.0f;
        const float span = hi_ - lo_;

        const float probability = std::clamp((float)mo.probability / 255.0f * motionMix +
                                                 mod_[pt::GT_MOTION_PROBABILITY] * modMix,
                                             0.0f, 1.0f);
        if (mv && probability > 0.0f && rnd() < probability) {   // trou
            lastSkipAt = head; lastSkipMs = nowMs;
            return;
        }
        const float chaos = mv ? std::clamp((float)mo.chaos / 255.0f * motionMix + mod_[pt::GT_CHAOS] * modMix, 0.0f, 1.0f)
                               : std::clamp(mod_[pt::GT_CHAOS], 0.0f, 1.0f);

        float start = head;   // POS + modulation + ROAM + LFO, deja calcule
        if (sprayHalf > 0.0f) start += rndb() * (sprayHalf * 2.0f);
        if (chaos > 0.0f)     start += rndb() * chaos * 0.25f * span;

        const float pJump = mv ? std::clamp((float)mo.jump / 255.0f * motionMix + mod_[pt::GT_JUMP] * modMix, 0.0f, 1.0f)
                               : std::clamp(mod_[pt::GT_JUMP], 0.0f, 1.0f);
        if (pJump > 0.0f && rnd() < pJump) start = lo_ + rnd() * span;

        const float hold = std::clamp((float)mo.hold / 255.0f * motionMix + mod_[pt::GT_MOTION_HOLD] * modMix, 0.0f, 1.0f);
        const float stutter = std::clamp(1.0f + (float)(mo.stutter - 1) * motionMix +
                                             mod_[pt::GT_MOTION_STUTTER] * 15.0f * modMix,
                                         1.0f, 16.0f);
        const int stutterCount = (int)std::lround(stutter);
        if (mv && haveLast_) {
            if (stutterLeft_ > 0) { start = lastStart_; --stutterLeft_; }
            else if (hold > 0.0f && rnd() < hold) start = lastStart_;
            else if (stutterCount > 1) stutterLeft_ = stutterCount - 1;
        } else if (mv && stutterCount > 1) {
            stutterLeft_ = stutterCount - 1;
        }
        start = std::clamp(start, lo_, hi_);
        lastStart_ = start; haveLast_ = true;

        float lifeMs = sizeMs;
        const float tunnel = std::clamp((float)mo.tunnel / 255.0f * motionMix + mod_[pt::GT_MOTION_TUNNEL] * modMix, 0.0f, 1.0f);
        if (mv && tunnel > 0.0f) lifeMs *= 1.0f + tunnel * 3.0f;
        if (chaos > 0.0f) lifeMs *= std::max(0.2f, 1.0f + rndb() * chaos * 0.7f);

        float st = semis;
        const float jit = std::clamp((float)gr.jitter / 255.0f + mod_[pt::GT_JITTER], 0.0f, 1.0f);
        if (jit > 0.0f)   st += rndb() * 12.0f * jit;
        if (chaos > 0.0f) st += rndb() * 7.0f * chaos;
        float step = std::pow(2.0f, st / 12.0f) * tapeMul_ / sampleSec_;   // 0..1 par seconde

        const float pRev = mv ? std::clamp((float)mo.rev / 255.0f * motionMix + mod_[pt::GT_REV] * modMix, 0.0f, 1.0f)
                              : std::clamp(mod_[pt::GT_REV], 0.0f, 1.0f);
        const bool rev = pRev > 0.0f && rnd() < pRev;
        if (rev) step = -step;

        dst->on = true; dst->pos = start; dst->step = step; dst->age = 0.0f;
        dst->life = std::max(0.002f, lifeMs * 0.001f);
        dst->win = std::clamp((int)std::lround((float)gr.window + mod_[pt::GT_GRAIN_WINDOW] * 4.0f), 0, 4);
        dst->rev = rev;
    }

    long long lastMs_ = -1;
    float lo_ = 0.0f, hi_ = 1.0f, sampleSec_ = 1.0f, timer_ = 0.0f;
    bool  sliced_ = false;
    uint32_t rng_ = 0x9E3779B9u;
    float mod_[pt::GT_COUNT] = {};
    float lfoPh_[8] = {}, shVal_[8] = {};
    float roamPh_ = 0, roamTarget_ = 0, walk_ = 0, walk2_ = 0, driftAcc_ = 0, roamSmooth_ = 0;
    float posLfo_ = 0, spinPh_ = 0, tapeT_ = 0, tapeMul_ = 1.0f;
    bool  haveLast_ = false;
    float lastStart_ = 0.0f;
    int   stutterLeft_ = 0;
};

}  // namespace pt::ui
