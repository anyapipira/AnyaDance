#pragma once

#include <memory>
#include <string>
#include <vector>

namespace anyadance::ui {

struct AudioOutputDevice {
    std::string id;
    std::string name;
    bool isDefault = false;
};

// Windows BGM playback for the MMD transport. Media Foundation decodes the
// selected file to PCM and XAudio2 sends it to the chosen render endpoint.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool Initialize(std::string& error);
    void Shutdown();

    bool RefreshOutputDevices(std::string& error);
    const std::vector<AudioOutputDevice>& OutputDevices() const;
    int SelectedOutputDevice() const;
    bool SelectOutputDevice(int index, std::string& error);
    bool SelectOutputDeviceById(const std::string& id, std::string& error);

    bool Load(const std::string& utf8Path, std::string& error);
    void Unload();
    bool HasAudio() const;
    double DurationSeconds() const;
    void SetVolume(float volume);
    float Volume() const;

    // Keep playback aligned to an external clock. Re-seeks only when starting,
    // looping, changing devices, or drifting materially from expectedSeconds.
    void Synchronize(double expectedSeconds, bool shouldPlay);
    void StopPlayback();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace anyadance::ui
