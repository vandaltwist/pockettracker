#include "sdl/sdl-audio-engine.h"

#include "audio-engine.h"
#include "latency_probe.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include "linux/alsa-route.h"
#include <unistd.h>
#endif
#if defined(__linux__) && defined(PT_HANDHELD)
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#endif

namespace {

// Frames per callback, 512 @ 48 kHz ≈ 10.7 ms — a latency choice; the engine chunks to its sub-block.
constexpr int FRAMES_PER_CALLBACK = 512;

constexpr int SMALL_FRAMES = 256;

constexpr int PREFERRED_RATE = AudioEngine::SAMPLE_RATE;

enum class RateSource { FIXED, ENV, PINNED };

const char* rate_source_text(RateSource s) {
    switch (s) {
        case RateSource::ENV:    return "env";
        case RateSource::PINNED: return "the engine's, kept";
        case RateSource::FIXED:  break;
    }
    return "fixed";
}

int requested_frames() {
    const char* v = std::getenv("POCKETTRACKER_AUDIO_FRAMES");
    if (v == nullptr || *v == '\0') return FRAMES_PER_CALLBACK;

    const long n = std::strtol(v, nullptr, 10);
    if (n < 32 || n > 8192) {
        std::fprintf(stderr, "POCKETTRACKER_AUDIO_FRAMES=%s out of range (32..8192), using %d\n", v,
                     FRAMES_PER_CALLBACK);
        return FRAMES_PER_CALLBACK;
    }
    int pow2 = 32;
    while (pow2 * 2 <= int(n)) pow2 *= 2;
    return pow2;
}

int requested_rate(RateSource& source) {
    if (const char* v = std::getenv("POCKETTRACKER_AUDIO_RATE"); v != nullptr && *v != '\0') {
        const long n = std::strtol(v, nullptr, 10);
        if (n >= 8000 && n <= 192000) {
            source = RateSource::ENV;
            return int(n);
        }
        std::fprintf(stderr, "POCKETTRACKER_AUDIO_RATE=%s out of range (8000..192000), ignoring\n", v);
    }

    source = RateSource::FIXED;
    return PREFERRED_RATE;
}

#if defined(__linux__) && !defined(__ANDROID__)
void choose_linux_route(int& frames) {
    const bool framesPinned = std::getenv("POCKETTRACKER_AUDIO_FRAMES") != nullptr;
    if (const char* dev = std::getenv("AUDIODEV")) {
        std::printf("audio:   route: AUDIODEV=%s given, not inspected\n", dev);
        return;
    }

    const char* driver = SDL_GetCurrentAudioDriver();
    if (driver && (std::strcmp(driver, "pipewire") == 0 || std::strcmp(driver, "pulseaudio") == 0)) {
        if (!framesPinned) frames = SMALL_FRAMES;
        std::printf("audio:   route: SDL talks to %s itself - a sound server, asking %d frames\n", driver,
                    frames);
        return;
    }
    if (!driver || std::strcmp(driver, "alsa") != 0) return;

    const ptshell::DefaultRoute r = ptshell::inspect_default_route();
    switch (r.route) {
        case ptshell::AlsaRoute::DIRECT:
#ifdef PT_HANDHELD
            setenv("AUDIODEV", r.hw.c_str(), 1);
            if (!framesPinned) frames = SMALL_FRAMES;
            std::printf("audio:   route: %sopening %s directly, asking %d frames\n", r.chain.c_str(),
                        r.hw.c_str(), frames);
#else
            std::printf("audio:   route: %sleft shared (not a handheld build)\n", r.chain.c_str());
#endif
            break;
        case ptshell::AlsaRoute::SERVER:
            if (!framesPinned) frames = SMALL_FRAMES;
            std::printf("audio:   route: %sa sound server, asking %d frames\n", r.chain.c_str(), frames);
            break;
        case ptshell::AlsaRoute::LEAVE:
            std::printf("audio:   route: %sleft as is\n", r.chain.c_str());
            break;
    }
}
#endif

#if defined(__linux__) && defined(PT_HANDHELD)
void raise_audio_thread_priority() {
    sched_param p{};
    p.sched_priority = 10;
    const int err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
    if (err == 0) std::printf("audio:   callback thread on SCHED_FIFO %d\n", p.sched_priority);
    else std::printf("audio:   callback thread left as SDL set it (SCHED_FIFO refused: %s)\n", std::strerror(err));
    std::fflush(stdout);
}
#endif

}  // namespace

SdlAudioEngine::SdlAudioEngine(AudioEngine* core) : core_(core) {}

SdlAudioEngine::~SdlAudioEngine() { closeStream(); }

void SDLCALL SdlAudioEngine::audioCallback(void* userdata, Uint8* out, int lenBytes) {
    auto* self = static_cast<SdlAudioEngine*>(userdata);

#if defined(__linux__) && defined(PT_HANDHELD)
    static bool priorityRaised = false;
    if (!priorityRaised) {
        priorityRaised = true;
        raise_audio_thread_priority();
    }
#endif

    const int numFrames = lenBytes / int(sizeof(float)) / self->channels_;

    latency::audio_callback(numFrames, self->sampleRate_);

    static const bool prof = (std::getenv("POCKETTRACKER_AUDIO_PROFILE") != nullptr);
    if (!prof) {
        self->core_->processLiveBlock(reinterpret_cast<float*>(out), numFrames, self->channels_,
                                    float(self->sampleRate_));
        return;
    }
    static uint64_t lastNs = 0, printNs = 0, maxBlk = 0, maxGap = 0, sumBlk = 0, cnt = 0, over = 0;
    uint64_t t0 = latency::now_ns();
    if (lastNs != 0) { uint64_t g = t0 - lastNs; if (g > maxGap) maxGap = g; }
    lastNs = t0;

    self->core_->processLiveBlock(reinterpret_cast<float*>(out), numFrames, self->channels_,
                                  float(self->sampleRate_));

    uint64_t t1 = latency::now_ns();
    uint64_t blk = t1 - t0;
    if (blk > maxBlk) maxBlk = blk;
    sumBlk += blk; ++cnt;
    uint64_t budgetNs = uint64_t(numFrames) * 1000000000ull / uint64_t(self->sampleRate_ ? self->sampleRate_ : 44100);
    if (blk > budgetNs) ++over;
    if (printNs == 0) printNs = t1;
    if (t1 - printNs >= 1000000000ull) {
        std::printf("PROF: n=%llu avgBlk=%.2fms maxBlk=%.2fms over=%llu maxGap=%.2fms budget=%.2fms frames=%d\n",
                    (unsigned long long)cnt, cnt ? double(sumBlk) / double(cnt) / 1e6 : 0.0,
                    double(maxBlk) / 1e6, (unsigned long long)over, double(maxGap) / 1e6,
                    double(budgetNs) / 1e6, numFrames);
        std::fflush(stdout);
        printNs = t1; maxBlk = 0; maxGap = 0; sumBlk = 0; cnt = 0; over = 0;
    }
}

bool SdlAudioEngine::openStream() {
    // closeCapture() appelle openStream() a chaque fin d'enregistrement, alors que le flux de lecture peut
    // deja etre ouvert (la capture reprise ne le ferme pas) : sans cette garde on ouvrirait un 2e peripherique
    // dont le callback tourne en parallele de l'autre sur le meme moteur.
    if (device_ != 0) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }

    int askedFrames = requested_frames();
#if defined(__linux__) && !defined(__ANDROID__)
    choose_linux_route(askedFrames);
#endif
    RateSource rateSource  = RateSource::FIXED;
    const int  askedRate   = pinnedRate_ > 0 ? pinnedRate_ : requested_rate(rateSource);
    if (pinnedRate_ > 0) rateSource = RateSource::PINNED;

    SDL_AudioSpec want{};
    want.freq     = askedRate;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = Uint16(askedFrames);
    want.callback = &SdlAudioEngine::audioCallback;
    want.userdata = this;

    SDL_AudioSpec got{};

    const int allow = SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, allow);

#ifndef _WIN32
    if (device_ == 0 && std::getenv("AUDIODEV") != nullptr) {
        std::fprintf(stderr, "audio:   AUDIODEV=%s failed (%s), trying the default device\n",
                     std::getenv("AUDIODEV"), SDL_GetError());
        unsetenv("AUDIODEV");
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, allow);
    }
#endif

    if (device_ == 0) {
        std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }
    if (got.format != AUDIO_F32SYS || got.channels != 2) {
        std::fprintf(stderr, "audio device is not stereo float32 (format=0x%04X, channels=%d)\n",
                     got.format, got.channels);
        closeStream();
        return false;
    }

    sampleRate_   = got.freq;
    channels_     = got.channels;
    bufferFrames_ = got.samples;

    if (core_) core_->setDeviceSampleRate(sampleRate_);

    SDL_PauseAudioDevice(device_, 0);

    const char* audiodev = std::getenv("AUDIODEV");
    std::printf("audio:   %d Hz, %d ch, %d frames/callback (%.1f ms at least), asked %d Hz (%s) / %d "
                "frames, driver=%s, device=%s\n",
                sampleRate_, channels_, bufferFrames_, 1000.0 * bufferFrames_ / sampleRate_,
                askedRate, rate_source_text(rateSource), askedFrames, SDL_GetCurrentAudioDriver(),
                audiodev != nullptr ? audiodev : "default");
    return true;
}

void SdlAudioEngine::closeStream() {
    shutdownCapture();
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

void SdlAudioEngine::resumeStream() {
    if (device_ != 0) SDL_PauseAudioDevice(device_, 0);
}

void SdlAudioEngine::setPaused(bool paused) {
    if (device_ != 0) SDL_PauseAudioDevice(device_, paused ? 1 : 0);
}

// ─── Capture (Corrigé en Mono) ─────────────────────────────────────────────────────────────────

void SDLCALL SdlAudioEngine::captureCallback(void* userdata, Uint8* stream, int lenBytes) {
    auto* self = static_cast<SdlAudioEngine*>(userdata);

    // Decode whatever SDL really gave us (format/channels come from the "obtained" spec).
    self->capCb_.fetch_add(1, std::memory_order_relaxed);
    const int ch = self->captureChannels > 0 ? self->captureChannels : 1;
    const SDL_AudioFormat fmt = self->captureFormat_;
    const int bytesPer = SDL_AUDIO_BITSIZE(fmt) / 8;
    const int numFrames = lenBytes / (bytesPer * ch);
    if (numFrames <= 0) return;

    // TrimUI Brick : seul MIC1 (canal gauche) est câblé, le droit est muet. POCKETTRACKER_CAPTURE_LEFT_ONLY=1
    // duplique le gauche sur les deux voies au lieu de laisser un canal vide (-6 dB, image décalée).
    static const bool leftOnly = [] { const char* v = std::getenv("POCKETTRACKER_CAPTURE_LEFT_ONLY"); return v && *v == '1'; }();

    static thread_local std::vector<float> leftBuf;
    static thread_local std::vector<float> rightBuf;
    if ((int)leftBuf.size() < numFrames) { leftBuf.resize(numFrames); rightBuf.resize(numFrames); }

    auto sampleAt = [&](int frame, int c) -> float {
        const int idx = frame * ch + c;
        if (fmt == AUDIO_F32SYS) return reinterpret_cast<const float*>(stream)[idx];
        if (fmt == AUDIO_S16SYS) return reinterpret_cast<const int16_t*>(stream)[idx] / 32768.0f;
        if (fmt == AUDIO_S32SYS) return reinterpret_cast<const int32_t*>(stream)[idx] / 2147483648.0f;
        if (fmt == AUDIO_U16SYS) return (reinterpret_cast<const uint16_t*>(stream)[idx] - 32768) / 32768.0f;
        if (fmt == AUDIO_U8)     return (stream[idx] - 128) / 128.0f;
        if (fmt == AUDIO_S8)     return reinterpret_cast<const int8_t*>(stream)[idx] / 128.0f;
        return 0.0f;
    };
    for (int i = 0; i < numFrames; ++i) {
        const float l = sampleAt(i, 0);
        const float r = (ch > 1 && !leftOnly) ? sampleAt(i, 1) : l;
        leftBuf[i] = l; rightBuf[i] = r;
    }
    // The engine's record buffer runs at the engine rate (sampleRate_). If the mic opened at another
    // rate (44100 vs 48000), resample here (linear) or the sample would play back at the wrong pitch.
    const int target = self->sampleRate_ > 0 ? self->sampleRate_ : 48000;
    {
        float pk = 0.0f;
        for (int i = 0; i < numFrames; ++i) { const float a = std::fabs(leftBuf[i]); if (a > pk) pk = a; }
        if (pk > self->capPeak_.load(std::memory_order_relaxed)) self->capPeak_.store(pk, std::memory_order_relaxed);
        self->capFrames_.fetch_add(numFrames, std::memory_order_relaxed);
    }
    if (self->captureRate_ > 0 && self->captureRate_ != target) {
        static thread_local std::vector<float> oL, oR;
        oL.clear(); oR.clear();
        const double step = double(self->captureRate_) / double(target);
        double pos = self->capPos_;
        while (true) {
            const int i0 = int(std::floor(pos));
            if (i0 + 1 > numFrames - 1) break;
            const float f = float(pos - i0);
            const float aL = i0 < 0 ? self->capPrevL_ : leftBuf[i0];
            const float aR = i0 < 0 ? self->capPrevR_ : rightBuf[i0];
            oL.push_back(aL + (leftBuf[i0 + 1]  - aL) * f);
            oR.push_back(aR + (rightBuf[i0 + 1] - aR) * f);
            pos += step;
        }
        self->capPos_   = pos - numFrames;
        self->capPrevL_ = leftBuf[numFrames - 1];
        self->capPrevR_ = rightBuf[numFrames - 1];
        if (!oL.empty()) self->core_->captureWriteImpl(oL.data(), oR.data(), int(oL.size()));
        return;
    }
    self->core_->captureWriteImpl(leftBuf.data(), rightBuf.data(), numFrames);
}


#if defined(__linux__) && !defined(__ANDROID__)
namespace {
struct AlsaCaptureCard { int num; std::string id; std::string driver; };

// Cards that can record, straight from the kernel: always current, whatever SDL saw at start-up.
std::vector<AlsaCaptureCard> scan_alsa_capture_cards() {
    std::vector<AlsaCaptureCard> out;
    std::FILE* f = std::fopen("/proc/asound/cards", "r");
    if (!f) return out;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        int  num = -1;
        char id[64] = {0}, drv[64] = {0};
        // " 1 [H5             ]: USB-Audio - H5"
        if (std::sscanf(line, " %d [%63[^]]]: %63s", &num, id, drv) != 3 || num < 0) continue;
        std::string sid = id;
        while (!sid.empty() && sid.back() == ' ') sid.pop_back();
        bool canCapture = false;
        for (int p = 0; p < 4 && !canCapture; ++p) {
            char path[96];
            std::snprintf(path, sizeof path, "/proc/asound/card%d/pcm%dc", num, p);
            canCapture = (::access(path, F_OK) == 0);
        }
        if (canCapture) out.push_back({num, sid, drv});
    }
    std::fclose(f);
    return out;
}
}  // namespace
#endif

bool SdlAudioEngine::openCapture(int deviceIndex) {
    // Which physical input this request means. A Linux ALSA card is named by its stable card id, so a
    // reordered list can never resume the wrong device.
    std::string reqKey = "sdl:" + std::to_string(deviceIndex);
    std::vector<std::string> alsaFirst;   // candidates for the chosen ALSA card, best first
    bool alsaStrict = false;              // an explicit non-internal choice never falls back to another mic
#if defined(__linux__) && !defined(__ANDROID__)
    {
        const auto cards = scan_alsa_capture_cards();
        inputCardNum_.clear(); inputCardId_.clear();
        for (const auto& cd : cards) { inputCardNum_.push_back(cd.num); inputCardId_.push_back(cd.id); }
        int pick = -1;
        if (deviceIndex >= 1 && deviceIndex <= static_cast<int>(cards.size())) {
            pick = deviceIndex - 1;
        } else if (deviceIndex <= 0 && !cards.empty()) {   // DEFAULT: the built-in codec when there is one
            pick = 0;
            for (size_t i = 0; i < cards.size(); ++i) if (cards[i].id == "audiocodec") { pick = static_cast<int>(i); break; }
        }
        if (pick >= 0) {
            const std::string n = std::to_string(cards[static_cast<size_t>(pick)].num);
            const bool internal = cards[static_cast<size_t>(pick)].id == "audiocodec";
            reqKey = "alsa:" + cards[static_cast<size_t>(pick)].id;
            // The codec refuses what the plug layer asks of it (EPERM, kernel "invalid rate setting"):
            // raw hw first. A USB card is the other way round.
            if (internal) alsaFirst = { "env:hw:" + n + ",0", "env:plughw:" + n + ",0" };
            else        { alsaFirst = { "env:plughw:" + n + ",0", "env:hw:" + n + ",0" }; alsaStrict = true; }
        }
    }
#endif
    // The INPUT cell may have changed since the last take: a capture kept open for another device must be
    // closed and reopened, or the old (internal) mic keeps recording whatever is selected.
    if (captureDevice_ != 0 && captureKey_ != reqKey) {
        std::fprintf(stderr, "audio:   INPUT changed (%s -> %s): reopening capture\n", captureKey_.c_str(), reqKey.c_str());
        shutdownCapture();
    }
    if (captureDevice_ != 0) {   // kept open from a previous record, same device: just resume it
        capPos_ = 0.0; capPrevL_ = capPrevR_ = 0.0f;
        capCb_ = 0; capFrames_ = 0; capPeak_ = 0.0f;
        SDL_PauseAudioDevice(captureDevice_, 0);
        std::fprintf(stderr, "audio:   capture resumed (%d Hz, %d ch)\n", captureRate_, captureChannels);
        std::fflush(stderr);
        return true;
    }

    // Sur le codec Allwinner (TrimUI Brick), la capture micro ne délivre pas de données si aucun
    // flux de lecture n'est ouvert : le codec ne se réveille que quand le DAC est actif. On ouvre
    // donc le périphérique de lecture avant la capture si nécessaire.
    if (device_ == 0) {
        openStream();
    }

#if defined(__linux__) && !defined(__ANDROID__)
    const char* saved = std::getenv("AUDIODEV");
    std::string savedDev;
    if (saved) { savedDev = saved; unsetenv("AUDIODEV"); }
#endif

    // Candidate device names, tried in order. A raw "hw:" device refuses anything but its native
    // format (EPERM / "Couldn't set hardware audio parameters"); "default" / "plughw:" go through
    // ALSA's plug layer which converts rate/format/channels for us.
    std::vector<std::string> candidates;
    const int count = SDL_GetNumAudioDevices(SDL_TRUE);
    std::fprintf(stderr, "audio:   %d capture device(s) seen by SDL (driver=%s)\n", count,
                 SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        const char* n = SDL_GetAudioDeviceName(i, SDL_TRUE);
        names.push_back(n ? n : "");
        std::fprintf(stderr, "audio:     [%d] %s\n", i, n ? n : "(null)");
    }
    // 0. La carte ALSA choisie (liste lue dans /proc/asound/cards), hw avant plughw pour le codec interne.
    candidates = alsaFirst;
    // 1. Sinon le périphérique sélectionné par l'utilisateur (via la cellule INPUT du sample editor) est essayé
    //    en premier. deviceIndex est 1-based : 1 = names[0], 2 = names[1], etc. 0 = DEFAULT.
    if (alsaFirst.empty() && deviceIndex >= 1 && deviceIndex <= count) candidates.push_back(names[deviceIndex - 1]);
    // Un périphérique choisi explicitement AUTRE que le micro interne est STRICT : s'il échoue on
    // renvoie une erreur (RECORD FAILED) au lieu de retomber en silence sur le micro interne.
    const bool strictChoice = alsaStrict || (alsaFirst.empty() && deviceIndex >= 2 && deviceIndex <= count);
    // 2. Puis les autres périphériques listés par SDL (sauf celui déjà ajouté ci-dessus).
    if (!strictChoice) {
    for (int i = 0; i < count; ++i) {
        if (deviceIndex >= 1 && deviceIndex <= count && i == deviceIndex - 1) continue;
        candidates.push_back(names[i]);
    }
    }
    // 3. En dernier recours : les périphériques ALSA bruts (plughw via dmix pour le mixage lecture+capture,
    //    puis hw en accès exclusif). Ceux-ci ne sont essayés que si aucun périphérique SDL n'a fonctionné.
#if defined(__linux__) && !defined(__ANDROID__)
    if (!strictChoice) {
    candidates.push_back("env:plughw:0,0");
    candidates.push_back("env:plughw:CARD=audiocodec,DEV=0");
    candidates.push_back("env:hw:0,0");
    candidates.push_back("env:hw:CARD=audiocodec,DEV=0");
    }
#endif
    // 4. Enfin le default SDL (PipeWire) en dernier recours.
    if (!strictChoice) candidates.push_back("");   // "" = nullptr: SDL default (PipeWire).
    std::fprintf(stderr, "audio:   INPUT request index=%d (%s), %zu candidate(s)\n", deviceIndex,
                 strictChoice ? "STRICT" : "with fallbacks", candidates.size());

    // Format/channel combinations to try per device: the mic may be stereo-only.
    struct Try { SDL_AudioFormat fmt; int ch; int freq; };
    const Try tries[] = {
        { AUDIO_S16SYS, 2, 48000 }, { AUDIO_S16SYS, 1, 48000 },
        { AUDIO_S16SYS, 2, 44100 }, { AUDIO_S16SYS, 1, 44100 },
    };

    SDL_AudioSpec got{};
    bool opened = false;
    for (int pass = 0; pass < 3 && !opened; ++pass) {
    if (pass > 0) { std::fprintf(stderr, "audio:   capture retry %d (device may still be releasing)\n", pass); SDL_Delay(200); }
    for (const auto& cand : candidates) {
        for (const auto& t : tries) {
            SDL_AudioSpec want{};
            want.freq     = t.freq;
            want.format   = t.fmt;
            want.channels = Uint8(t.ch);
            // Capture needs no low latency: a 2048-frame period (~43 ms) gives the capture thread room to
            // be late under load (viz FFT, UI) without an ALSA overrun that drops audio.
            want.samples  = Uint16(2048);
            want.callback = &SdlAudioEngine::captureCallback;
            want.userdata = this;
            SDL_zero(got);
            // Set the members the callback reads BEFORE the device can start (it starts paused anyway).
            const bool viaEnv = cand.compare(0, 4, "env:") == 0;
            const std::string devName = viaEnv ? cand.substr(4) : cand;
#if defined(__linux__) && !defined(__ANDROID__)
            if (viaEnv) setenv("AUDIODEV", devName.c_str(), 1); else unsetenv("AUDIODEV");
#endif
            captureDevice_ = SDL_OpenAudioDevice((viaEnv || devName.empty()) ? nullptr : devName.c_str(), 1, &want, &got,
                                                 SDL_AUDIO_ALLOW_ANY_CHANGE);
#if defined(__linux__) && !defined(__ANDROID__)
            unsetenv("AUDIODEV");
#endif
            if (captureDevice_ != 0) {
                // Les membres lus par le callback sont posés AVANT de démarrer le flux.
                captureRate_ = got.freq; captureChannels = got.channels; captureFormat_ = got.format;
                capPos_ = 0.0; capPrevL_ = capPrevR_ = 0.0f;
                capCb_ = 0; capFrames_ = 0; capPeak_ = 0.0f;
                SDL_PauseAudioDevice(captureDevice_, 0);
                bool alive = false;
                for (int w = 0; w < 20 && !alive; ++w) { SDL_Delay(20); alive = capCb_.load() > 0; }
                if (alive) { opened = true; break; }
                std::fprintf(stderr, "audio:   capture '%s' (%dch %dHz) opened but delivers NO data, trying next\n",
                             cand.empty() ? "(null)" : cand.c_str(), t.ch, t.freq);
                SDL_PauseAudioDevice(captureDevice_, 1);
                SDL_CloseAudioDevice(captureDevice_);
                captureDevice_ = 0;
                break;   // ce périphérique est muet : inutile d'essayer d'autres formats dessus
            }
            std::fprintf(stderr, "audio:   capture open '%s' (%dch %dHz) failed: %s\n",
                         cand.empty() ? "(null)" : cand.c_str(), t.ch, t.freq, SDL_GetError());
        }
        if (opened) {
            std::printf("audio:   capture using '%s'\n", cand.empty() ? "(sdl default)" : cand.c_str());
            std::fflush(stdout);
            break;
        }
    }
    }

    if (!opened) {
        std::fprintf(stderr, "SDL capture open failed: %s\n", SDL_GetError());
#if defined(__linux__) && !defined(__ANDROID__)
        if (!savedDev.empty()) setenv("AUDIODEV", savedDev.c_str(), 1);
#endif
        return false;
    }

    captureKey_ = reqKey;
    capPos_ = 0.0; capPrevL_ = capPrevR_ = 0.0f;
    captureRate_    = got.freq;
    captureChannels = got.channels;
    captureFormat_  = got.format;

    SDL_PauseAudioDevice(captureDevice_, 0);
    std::printf("audio:   capture device opened (%d Hz, %d ch, fmt=0x%04X)\n",
                captureRate_, captureChannels, got.format);
    std::fflush(stdout);   // sans ca, un abort() perd ces lignes (stdout est bufferise dans le tee du lanceur)

#if defined(__linux__) && !defined(__ANDROID__)
    if (!savedDev.empty()) setenv("AUDIODEV", savedDev.c_str(), 1);
#endif
    return true;
}

void SdlAudioEngine::closeCapture() {
    // Only PAUSE: reopening the ALSA/PipeWire capture on every record crashed on the 3rd attempt on the
    // TrimUI Brick. The device stays open (cheap, nothing is written while recordActive is false) and is
    // really closed in closeStream().
    if (captureDevice_ != 0) {
        SDL_PauseAudioDevice(captureDevice_, 1);
        std::fprintf(stderr, "audio:   capture paused (callbacks=%d frames=%d peak=%.4f)\n",
                     capCb_.load(), capFrames_.load(), double(capPeak_.load()));
        std::fflush(stderr);
    }
}

void SdlAudioEngine::shutdownCapture() {
    if (captureDevice_ != 0) {
        SDL_PauseAudioDevice(captureDevice_, 1);
        SDL_CloseAudioDevice(captureDevice_);
        captureDevice_ = 0;
    }
    captureKey_.clear();
}

int SdlAudioEngine::captureWrite(const float* left, const float* right, int frames) {
    if (core_) core_->captureWriteImpl(left, right, frames);
    return frames;
}

void SdlAudioEngine::listInputDevices(std::vector<std::string>& out) {
    out.clear();
#if defined(__linux__) && !defined(__ANDROID__)
    // The cards the kernel knows NOW (a USB mic plugged after start-up included). The names start with a
    // distinct 3-letter tag because the sample editor's INPUT cell shows only those: MIC = built-in, USB.
    inputCardNum_.clear(); inputCardId_.clear();
    for (const auto& cd : scan_alsa_capture_cards()) {
        inputCardNum_.push_back(cd.num);
        inputCardId_.push_back(cd.id);
        if (cd.id == "audiocodec")           out.push_back("MIC TRIMUI");
        else if (cd.driver == "USB-Audio")   out.push_back("USB " + cd.id);
        else                                 out.push_back("CARD " + cd.id);
        std::fprintf(stderr, "audio:   input [%zu] card %d id=%s driver=%s -> '%s'\n", out.size(), cd.num,
                     cd.id.c_str(), cd.driver.c_str(), out.back().c_str());
    }
    std::fflush(stderr);
    if (!out.empty()) return;   // nothing readable in /proc/asound: fall back to SDL's own list below
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    const char* saved = std::getenv("AUDIODEV");
    std::string savedDev;
    if (saved) { savedDev = saved; unsetenv("AUDIODEV"); }
#endif
    const char* driver = SDL_GetCurrentAudioDriver();
    const int count = SDL_GetNumAudioDevices(SDL_TRUE);
    for (int i = 0; i < count; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(i, SDL_TRUE)) {
            // Provide user-friendly display names
            std::string displayName = name;
            
            // Friendly naming for TrimUI internal microphone
            if (i == 0) {
                displayName = "Micro TrimUI (Interne)";
            } else if (std::string(name).find("capture") != std::string::npos ||
                       std::string(name).find("mic") != std::string::npos ||
                       std::string(name).find("default") != std::string::npos) {
                displayName = "Micro externe";
            }
            
            out.push_back(displayName);
        }
    }
#if defined(__linux__) && !defined(__ANDROID__)
    if (!savedDev.empty()) setenv("AUDIODEV", savedDev.c_str(), 1);
#endif
}