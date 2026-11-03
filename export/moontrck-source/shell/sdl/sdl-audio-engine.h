// The SDL2 audio backend: opens a stereo float stream and hands each buffer to
// AudioEngine::processLiveBlock(). ⚠️ Glue only — no DSP here.

#ifndef POCKETTRACKER_SDL_AUDIO_ENGINE_H
#define POCKETTRACKER_SDL_AUDIO_ENGINE_H

// <cmath> before <SDL.h>: the engine defines _USE_MATH_DEFINES (MSVC's M_PI) and SDL_stdinc.h defines
// M_PI behind an #ifndef — the other order warns C4005 on every MSVC build.
#include <cmath>

#include <SDL.h>

#include <atomic>
#include <string>
#include <vector>

#include "audio-backend.h"

class AudioEngine;

class SdlAudioEngine : public AudioBackend {
  public:
    explicit SdlAudioEngine(AudioEngine* core);
    ~SdlAudioEngine() override;

    // Stereo float32 or false — processAudioBlock is stereo-only, and an SDL conversion would be worse
    // than failing loudly.
    bool openStream() override;
    void closeStream() override;

    // AudioEngine::onResumeRequested: unpause the device.
    void resumeStream() override;

    // For an offline render. ⚠️ `renderOffline` rewrites the buffers the callback reads; pausing blocks
    // until the callback has returned, so one writer by construction, not by timing.
    void setPaused(bool paused) override;

    // The negotiated rate, not the one asked for.
    int sampleRate() const override { return sampleRate_; }

    // Always a floor (`measured` false): SDL2 cannot report device latency, so this is the app's own
    // buffer and the driver's queue is left to a microphone.
    OutputLatency outputLatency() const override { return {bufferFrames_, false}; }

    // Open at `hz` from now on; 0 = the engine's rate (or the env override). For a mid-session reopen:
    // loaded samples are pitched for the rate in force.
    void pin_rate(int hz) { pinnedRate_ = hz; }

    // ── Capture ───────────────────────────────────────────────────────────────────────────────────
    bool openCapture(int deviceIndex) override;
    void closeCapture() override;
    void shutdownCapture();
    int  captureWrite(const float* left, const float* right, int frames) override;
    void listInputDevices(std::vector<std::string>& out) override;

  private:
    static void SDLCALL audioCallback(void* userdata, Uint8* out, int lenBytes);
    static void SDLCALL captureCallback(void* userdata, Uint8* stream, int lenBytes);

    AudioEngine*      core_         = nullptr;
    SDL_AudioDeviceID device_       = 0;
    int               sampleRate_   = 0;
    int               channels_     = 0;
    int               bufferFrames_ = 0;  // what the device chose
    int               pinnedRate_   = 0;

    // Capture device and its sink into AudioEngine's record buffer.
    SDL_AudioDeviceID captureDevice_ = 0;
    std::string       captureKey_;               // which input the open capture belongs to ("" = none)
    // Linux: the capture cards read from /proc/asound/cards by the last listInputDevices(), in the order
    // the INPUT cell shows them. SDL's own list is a snapshot taken at start-up, so a USB mic plugged in
    // later never reaches it.
    std::vector<int>         inputCardNum_;
    std::vector<std::string> inputCardId_;
    int               captureRate_   = 0;
    int               captureChannels = 0;
    SDL_AudioFormat   captureFormat_ = AUDIO_S16SYS;
    double            capPos_ = 0.0;
    std::atomic<int>   capCb_{0}, capFrames_{0};
    std::atomic<float> capPeak_{0.0f};
    float             capPrevL_ = 0.0f, capPrevR_ = 0.0f;
};

#endif  // POCKETTRACKER_SDL_AUDIO_ENGINE_H
