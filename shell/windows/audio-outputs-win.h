// SETTINGS > AUDIO OUT on Windows: one AudioBackend delegating to the system output or an ASIO driver.
// An empty TU elsewhere.
#ifndef POCKETTRACKER_AUDIO_OUTPUTS_WIN_H
#define POCKETTRACKER_AUDIO_OUTPUTS_WIN_H

#ifdef _WIN32

#include <string>
#include <vector>

#include "app.h"
#include "windows/asio-audio-engine.h"
#include "sdl/sdl-audio-engine.h"

namespace ptshell {

class WindowsAudioOutputs : public AudioBackend, public AudioOutputSelector {
  public:
    // `system` must already be open. The ASIO drivers are listed once, here.
    WindowsAudioOutputs(AudioEngine* core, SdlAudioEngine& system);

    const std::vector<std::string>& names() const override { return names_; }
    int  active() const override { return active_; }
    bool select(int index, std::string& error) override;

    bool openStream() override;
    void closeStream() override { current().closeStream(); }
    void resumeStream() override { current().resumeStream(); }
    void setPaused(bool paused) override { current().setPaused(paused); }
    int  sampleRate() const override { return current().sampleRate(); }
    OutputLatency outputLatency() const override { return current().outputLatency(); }
    bool deviceLost() const override { return current().deviceLost(); }

    // Audio input capture is handled by the system (SDL) backend — ASIO output switching does not
    // affect capture. Delegate so SETTINGS / SAMPLE EDITOR can list and open input devices.
    bool openCapture(int deviceIndex) override { return system_.openCapture(deviceIndex); }
    void closeCapture() override { system_.closeCapture(); }
    int  captureWrite(const float* left, const float* right, int frames) override {
        return system_.captureWrite(left, right, frames);
    }
    void listInputDevices(std::vector<std::string>& out) override { system_.listInputDevices(out); }

  private:
    AudioBackend&       current();
    const AudioBackend& current() const;
    bool open(int index, int rate);

    AudioEngine*             core_;
    SdlAudioEngine&          system_;
    AsioAudioEngine          asio_;
    std::vector<std::string> names_;
    int                      active_ = 0;
};

}  // namespace ptshell

#endif  // _WIN32
#endif  // POCKETTRACKER_AUDIO_OUTPUTS_WIN_H
