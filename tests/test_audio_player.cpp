#include "test_framework.h"
#include "tests.h"

#include "ui/audio_player.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace anyadance::tests {
namespace {

template <typename T>
void WriteLittleEndian(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool WriteTestWave(const std::string& path) {
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t frameCount = 800;  // 0.1 seconds
    constexpr std::uint16_t blockAlign = channels * bitsPerSample / 8;
    constexpr std::uint32_t dataBytes = frameCount * blockAlign;
    constexpr std::uint32_t riffBytes = 36 + dataBytes;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write("RIFF", 4);
    WriteLittleEndian(out, riffBytes);
    out.write("WAVEfmt ", 8);
    WriteLittleEndian(out, std::uint32_t{16});
    WriteLittleEndian(out, std::uint16_t{1});  // PCM
    WriteLittleEndian(out, channels);
    WriteLittleEndian(out, sampleRate);
    WriteLittleEndian(out, std::uint32_t{sampleRate * blockAlign});
    WriteLittleEndian(out, blockAlign);
    WriteLittleEndian(out, bitsPerSample);
    out.write("data", 4);
    WriteLittleEndian(out, dataBytes);
    for (std::uint32_t i = 0; i < frameCount; ++i) {
        WriteLittleEndian(out, std::int16_t{0});
    }
    return out.good();
}

} // namespace

void TestAudioPlayer() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ui::AudioPlayer player;
    std::string error;
    // GitHub-hosted runners may have no active render endpoint. In that case the
    // endpoint-dependent runtime portion is intentionally skipped, but the full
    // Media Foundation/XAudio2 backend is still compiled and linked by this test.
    if (!player.Initialize(error)) {
        EXPECT_TRUE(!error.empty());
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return;
    }

    EXPECT_TRUE(!player.OutputDevices().empty());
    EXPECT_TRUE(player.SelectedOutputDevice() >= 0);

    char tempDirectory[MAX_PATH] = {};
    EXPECT_TRUE(GetTempPathA(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory) > 0);
    const std::string path = std::string(tempDirectory) +
        "AnyaDance_audio_test_" + std::to_string(GetCurrentProcessId()) + ".wav";
    EXPECT_TRUE(WriteTestWave(path));
    if (player.Load(path, error)) {
        EXPECT_TRUE(player.HasAudio());
        EXPECT_NEAR(player.DurationSeconds(), 0.1, 0.01);
    } else {
        EXPECT_TRUE(false);
    }
    player.Shutdown();
    DeleteFileA(path.c_str());
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

} // namespace anyadance::tests
