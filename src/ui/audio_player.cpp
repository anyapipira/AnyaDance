#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "ui/audio_player.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace anyadance::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaxDecodedAudioBytes = 512ull * 1024ull * 1024ull;
constexpr double kResyncThresholdSeconds = 0.080;

std::string HResultMessage(const char* operation, HRESULT hr) {
    char systemMessage[512] = {};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        0,
        systemMessage,
        static_cast<DWORD>(std::size(systemMessage)),
        nullptr);
    std::string message = operation;
    message += " failed (0x";
    char code[16] = {};
    std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(hr));
    message += code;
    message += ")";
    if (systemMessage[0] != '\0') {
        message += ": ";
        message += systemMessage;
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
            message.pop_back();
        }
    }
    return message;
}

std::wstring Widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        wide.data(), count);
    return wide;
}

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        narrow.data(), count, nullptr, nullptr);
    return narrow;
}

} // namespace

struct AudioPlayer::Impl {
    ComPtr<IXAudio2> xaudio;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    IXAudio2SourceVoice* sourceVoice = nullptr;
    std::vector<AudioOutputDevice> devices;
    int selectedDevice = -1;
    std::vector<std::uint8_t> waveFormatBytes;
    std::vector<std::uint8_t> decodedAudio;
    double durationSeconds = 0.0;
    std::uint64_t sourceStartFrame = 0;
    bool initialized = false;
    bool mediaFoundationStarted = false;

    WAVEFORMATEX* WaveFormat() {
        return waveFormatBytes.empty()
            ? nullptr
            : reinterpret_cast<WAVEFORMATEX*>(waveFormatBytes.data());
    }

    const WAVEFORMATEX* WaveFormat() const {
        return waveFormatBytes.empty()
            ? nullptr
            : reinterpret_cast<const WAVEFORMATEX*>(waveFormatBytes.data());
    }

    void DestroySourceVoice() {
        if (sourceVoice) {
            sourceVoice->Stop(0);
            sourceVoice->DestroyVoice();
            sourceVoice = nullptr;
        }
        sourceStartFrame = 0;
    }

    void DestroyMasteringVoice() {
        DestroySourceVoice();
        if (masteringVoice) {
            masteringVoice->DestroyVoice();
            masteringVoice = nullptr;
        }
    }

    bool CreateMasteringVoice(const std::wstring& deviceId, std::string& error) {
        DestroyMasteringVoice();
        const HRESULT hr = xaudio->CreateMasteringVoice(
            &masteringVoice,
            XAUDIO2_DEFAULT_CHANNELS,
            XAUDIO2_DEFAULT_SAMPLERATE,
            0,
            deviceId.empty() ? nullptr : deviceId.c_str());
        if (FAILED(hr)) {
            error = HResultMessage("CreateMasteringVoice", hr);
            return false;
        }
        return true;
    }

    bool StartAt(double seconds) {
        DestroySourceVoice();
        WAVEFORMATEX* format = WaveFormat();
        if (!format || decodedAudio.empty() || !masteringVoice || format->nSamplesPerSec == 0 ||
            format->nBlockAlign == 0) {
            return false;
        }

        const double clamped = std::clamp(seconds, 0.0, durationSeconds);
        const auto frame = static_cast<std::uint64_t>(
            std::floor(clamped * static_cast<double>(format->nSamplesPerSec)));
        const std::uint64_t totalFrames = decodedAudio.size() / format->nBlockAlign;
        sourceStartFrame = std::min(frame, totalFrames);
        const std::uint64_t byteOffset = sourceStartFrame * format->nBlockAlign;
        if (byteOffset >= decodedAudio.size()) {
            return false;
        }

        HRESULT hr = xaudio->CreateSourceVoice(&sourceVoice, format);
        if (FAILED(hr)) {
            sourceVoice = nullptr;
            return false;
        }

        XAUDIO2_BUFFER buffer{};
        const std::size_t remaining = decodedAudio.size() - static_cast<std::size_t>(byteOffset);
        buffer.AudioBytes = static_cast<UINT32>(
            std::min<std::size_t>(remaining, std::numeric_limits<UINT32>::max()));
        buffer.pAudioData = decodedAudio.data() + byteOffset;
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        hr = sourceVoice->SubmitSourceBuffer(&buffer);
        if (SUCCEEDED(hr)) {
            hr = sourceVoice->Start(0);
        }
        if (FAILED(hr)) {
            DestroySourceVoice();
            return false;
        }
        return true;
    }
};

AudioPlayer::AudioPlayer() : m_impl(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() {
    Shutdown();
}

bool AudioPlayer::Initialize(std::string& error) {
    if (m_impl->initialized) {
        return true;
    }

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        error = HResultMessage("MFStartup", hr);
        return false;
    }
    m_impl->mediaFoundationStarted = true;

    hr = XAudio2Create(&m_impl->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        error = HResultMessage("XAudio2Create", hr);
        Shutdown();
        return false;
    }

    m_impl->initialized = true;
    std::string refreshError;
    if (!RefreshOutputDevices(refreshError)) {
        error = refreshError;
        Shutdown();
        return false;
    }
    if (m_impl->devices.empty()) {
        error = "No active audio output device was found.";
        return false;
    }
    return SelectOutputDevice(0, error);
}

void AudioPlayer::Shutdown() {
    if (!m_impl) {
        return;
    }
    m_impl->DestroyMasteringVoice();
    m_impl->decodedAudio.clear();
    m_impl->waveFormatBytes.clear();
    m_impl->durationSeconds = 0.0;
    m_impl->devices.clear();
    m_impl->selectedDevice = -1;
    m_impl->xaudio.Reset();
    m_impl->initialized = false;
    if (m_impl->mediaFoundationStarted) {
        MFShutdown();
        m_impl->mediaFoundationStarted = false;
    }
}

bool AudioPlayer::RefreshOutputDevices(std::string& error) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = HResultMessage("MMDeviceEnumerator", hr);
        return false;
    }

    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id)) && id) {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        error = HResultMessage("EnumAudioEndpoints", hr);
        return false;
    }

    UINT count = 0;
    collection->GetCount(&count);
    std::vector<AudioOutputDevice> devices;
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) {
            continue;
        }
        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId)) || !rawId) {
            continue;
        }
        const std::wstring id(rawId);
        CoTaskMemFree(rawId);

        std::wstring name = id;
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                value.vt == VT_LPWSTR && value.pwszVal) {
                name = value.pwszVal;
            }
            PropVariantClear(&value);
        }
        devices.push_back({Narrow(id), Narrow(name), id == defaultId});
    }

    std::stable_sort(devices.begin(), devices.end(), [](const AudioOutputDevice& a, const AudioOutputDevice& b) {
        return a.isDefault && !b.isDefault;
    });
    m_impl->devices = std::move(devices);
    // Refreshing invalidates the old vector index. The caller may restore the
    // same endpoint by its stable device ID after the refresh completes.
    m_impl->selectedDevice = -1;
    return true;
}

const std::vector<AudioOutputDevice>& AudioPlayer::OutputDevices() const {
    return m_impl->devices;
}

int AudioPlayer::SelectedOutputDevice() const {
    return m_impl->selectedDevice;
}

bool AudioPlayer::SelectOutputDevice(int index, std::string& error) {
    if (!m_impl->xaudio) {
        error = "The Windows audio engine is not initialized.";
        return false;
    }
    if (index < 0 || index >= static_cast<int>(m_impl->devices.size())) {
        error = "Audio output device selection is out of range.";
        return false;
    }
    const std::wstring deviceId = Widen(m_impl->devices[static_cast<std::size_t>(index)].id);
    m_impl->selectedDevice = -1;
    if (!m_impl->CreateMasteringVoice(deviceId, error)) {
        return false;
    }
    m_impl->selectedDevice = index;
    return true;
}

bool AudioPlayer::SelectOutputDeviceById(const std::string& id, std::string& error) {
    for (std::size_t i = 0; i < m_impl->devices.size(); ++i) {
        if (m_impl->devices[i].id == id) {
            return SelectOutputDevice(static_cast<int>(i), error);
        }
    }
    error = "The saved audio output device is not currently available.";
    return false;
}

bool AudioPlayer::Load(const std::string& utf8Path, std::string& error) {
    StopPlayback();
    m_impl->decodedAudio.clear();
    m_impl->waveFormatBytes.clear();
    m_impl->durationSeconds = 0.0;

    const std::wstring path = Widen(utf8Path);
    if (path.empty()) {
        error = "The audio path is empty or invalid UTF-8.";
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        error = HResultMessage("Open audio file", hr);
        return false;
    }
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> requestedType;
    hr = MFCreateMediaType(&requestedType);
    if (SUCCEEDED(hr)) {
        hr = requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    }
    if (SUCCEEDED(hr)) {
        hr = requestedType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    }
    if (SUCCEEDED(hr)) {
        hr = reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requestedType.Get());
    }
    if (FAILED(hr)) {
        error = HResultMessage("Configure audio decoder", hr);
        return false;
    }

    ComPtr<IMFMediaType> decodedType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &decodedType);
    if (FAILED(hr)) {
        error = HResultMessage("Read decoded audio format", hr);
        return false;
    }
    WAVEFORMATEX* rawFormat = nullptr;
    UINT32 rawFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(
        decodedType.Get(), &rawFormat, &rawFormatSize, MFWaveFormatExConvertFlag_Normal);
    if (FAILED(hr) || !rawFormat || rawFormatSize < sizeof(WAVEFORMATEX)) {
        if (rawFormat) {
            CoTaskMemFree(rawFormat);
        }
        error = HResultMessage("Convert decoded audio format", hr);
        return false;
    }
    m_impl->waveFormatBytes.assign(
        reinterpret_cast<std::uint8_t*>(rawFormat),
        reinterpret_cast<std::uint8_t*>(rawFormat) + rawFormatSize);
    CoTaskMemFree(rawFormat);

    while (true) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) {
            error = HResultMessage("Decode audio sample", hr);
            Unload();
            return false;
        }
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) {
                error = HResultMessage("Read decoded audio sample", hr);
                Unload();
                return false;
            }
            BYTE* bytes = nullptr;
            DWORD length = 0;
            hr = buffer->Lock(&bytes, nullptr, &length);
            if (FAILED(hr)) {
                error = HResultMessage("Lock decoded audio sample", hr);
                Unload();
                return false;
            }
            if (m_impl->decodedAudio.size() + length > kMaxDecodedAudioBytes) {
                buffer->Unlock();
                error = "Decoded audio exceeds the 512 MiB safety limit.";
                Unload();
                return false;
            }
            m_impl->decodedAudio.insert(m_impl->decodedAudio.end(), bytes, bytes + length);
            buffer->Unlock();
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
    }

    const WAVEFORMATEX* format = m_impl->WaveFormat();
    if (!format || format->nBlockAlign == 0 || format->nSamplesPerSec == 0 ||
        m_impl->decodedAudio.empty()) {
        error = "The audio file decoded to no playable PCM frames.";
        Unload();
        return false;
    }
    m_impl->decodedAudio.resize(
        m_impl->decodedAudio.size() - (m_impl->decodedAudio.size() % format->nBlockAlign));
    if (m_impl->decodedAudio.empty()) {
        error = "The audio file decoded to no complete PCM frames.";
        Unload();
        return false;
    }
    const std::uint64_t frameCount = m_impl->decodedAudio.size() / format->nBlockAlign;
    m_impl->durationSeconds = static_cast<double>(frameCount) / format->nSamplesPerSec;
    return m_impl->durationSeconds > 0.0;
}

void AudioPlayer::Unload() {
    StopPlayback();
    m_impl->decodedAudio.clear();
    m_impl->waveFormatBytes.clear();
    m_impl->durationSeconds = 0.0;
}

bool AudioPlayer::HasAudio() const {
    return !m_impl->decodedAudio.empty() && m_impl->durationSeconds > 0.0;
}

double AudioPlayer::DurationSeconds() const {
    return m_impl->durationSeconds;
}

void AudioPlayer::Synchronize(double expectedSeconds, bool shouldPlay) {
    if (!shouldPlay || !HasAudio()) {
        StopPlayback();
        return;
    }
    expectedSeconds = std::clamp(expectedSeconds, 0.0, m_impl->durationSeconds);
    const WAVEFORMATEX* format = m_impl->WaveFormat();
    if (!format || format->nSamplesPerSec == 0) {
        return;
    }
    if (!m_impl->sourceVoice) {
        m_impl->StartAt(expectedSeconds);
        return;
    }

    XAUDIO2_VOICE_STATE state{};
    m_impl->sourceVoice->GetState(&state, 0);
    const double actualSeconds = static_cast<double>(m_impl->sourceStartFrame + state.SamplesPlayed) /
        static_cast<double>(format->nSamplesPerSec);
    if (state.BuffersQueued == 0 || std::abs(actualSeconds - expectedSeconds) > kResyncThresholdSeconds) {
        m_impl->StartAt(expectedSeconds);
    }
}

void AudioPlayer::StopPlayback() {
    m_impl->DestroySourceVoice();
}

} // namespace anyadance::ui
