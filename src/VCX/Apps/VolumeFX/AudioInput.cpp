#include "Apps/VolumeFX/AudioInput.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#undef APIENTRY

#pragma comment(lib, "winmm.lib")

#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/trigonometric.hpp>

namespace VCX::Apps::VolumeFX {
    namespace {
        constexpr float kMinBaseGain = 0.1f;
        constexpr float kMaxBaseGain = 4.0f;
        constexpr float kMinVizGain  = 0.05f;
        constexpr float kMaxVizGain  = 8.0f;
    }

    AudioInput::AudioInput() {
        _status = "No audio loaded.";
    }

    AudioInput::~AudioInput() {
        stopPlayback();
    }

    void AudioInput::stopPlayback() {
        if (_playing) {
            PlaySoundW(nullptr, nullptr, 0);
        }
        _playing = false;
    }

    void AudioInput::startPlayback() {
        stopPlayback();
        if (_pathW.empty()) {
            return;
        }

        UINT flags = SND_FILENAME | SND_ASYNC;
        if (_loop) {
            flags |= SND_LOOP;
        }
        if (! PlaySoundW(_pathW.c_str(), nullptr, flags)) {
            _status = "Failed to play audio.";
            _playing = false;
        } else {
            _playing = true;
        }
    }

    bool AudioInput::LoadFromPath(const char * path) {
        if (path == nullptr || path[0] == '\0') {
            _status = "Please provide a file path.";
            return false;
        }

        bool const ok = loadAudioFile(path);
        if (ok) {
            startPlayback();
        }
        return ok;
    }

    bool AudioInput::loadAudioFile(const char * path) {
        stopPlayback();
        _loaded = false;
        _audioEnvelope.clear();
        _sampleRate = 0.0f;
        _duration = 0.0f;
        _currentLevel = 0.0f;
        _mockPlaybackTime = 0.0f;

        namespace fs = std::filesystem;
        fs::path resolved = fs::u8path(path);
        if (resolved.is_relative()) {
            std::error_code ec;
            resolved = fs::current_path(ec) / resolved;
        }

        std::ifstream file(resolved, std::ios::binary);
        if (! file) {
            _status = "Unable to open audio file.";
            return false;
        }

        auto readBytes = [&](void * dst, std::size_t size) {
            return static_cast<bool>(file.read(static_cast<char *>(dst), static_cast<std::streamsize>(size)));
        };

        char riff[4];
        if (! readBytes(riff, 4) || std::strncmp(riff, "RIFF", 4) != 0) {
            _status = "Invalid WAV: missing RIFF header.";
            return false;
        }

        [[maybe_unused]] std::uint32_t riffSize = 0;
        if (! readBytes(&riffSize, 4)) {
            _status = "Invalid WAV: truncated size.";
            return false;
        }

        char wave[4];
        if (! readBytes(wave, 4) || std::strncmp(wave, "WAVE", 4) != 0) {
            _status = "Invalid WAV: missing WAVE tag.";
            return false;
        }

        struct ChunkHeader {
            char          id[4];
            std::uint32_t size;
        };

        struct FmtChunkData {
            std::uint16_t audioFormat = 0;
            std::uint16_t numChannels = 0;
            std::uint32_t sampleRate = 0;
            std::uint32_t byteRate = 0;
            std::uint16_t blockAlign = 0;
            std::uint16_t bitsPerSample = 0;
        };

        FmtChunkData fmt { };
        bool fmtFound = false;
        bool dataFound = false;
        std::vector<char> audioData;

        auto skipPadding = [&](std::uint32_t size) {
            if ((size & 1u) != 0u) {
                file.seekg(1, std::ios::cur);
            }
        };

        ChunkHeader chunk { };
        while (readBytes(&chunk, sizeof(chunk))) {
            std::string chunkId(chunk.id, 4);
            if (chunkId == "fmt ") {
                std::vector<char> buffer(chunk.size);
                if (! buffer.empty() && ! readBytes(buffer.data(), buffer.size())) {
                    _status = "Invalid WAV: truncated fmt chunk.";
                    return false;
                }
                if (chunk.size < 16) {
                    _status = "Invalid WAV: fmt chunk too small.";
                    return false;
                }
                std::size_t const bytesToCopy = std::min<std::size_t>(sizeof(FmtChunkData), buffer.size());
                std::memcpy(&fmt, buffer.data(), bytesToCopy);
                fmtFound = true;
            } else if (chunkId == "data") {
                audioData.resize(chunk.size);
                if (! audioData.empty() && ! readBytes(audioData.data(), audioData.size())) {
                    _status = "Invalid WAV: truncated data chunk.";
                    return false;
                }
                dataFound = true;
                break;
            } else {
                file.seekg(static_cast<std::streamoff>(chunk.size), std::ios::cur);
            }

            skipPadding(chunk.size);
        }

        if (! fmtFound || ! dataFound) {
            _status = "Invalid WAV: missing fmt or data chunk.";
            return false;
        }

        if (fmt.audioFormat != 1 || fmt.bitsPerSample != 16) {
            _status = "Only PCM16 WAV files are supported.";
            return false;
        }

        if (fmt.numChannels == 0 || fmt.blockAlign == 0 || fmt.sampleRate == 0) {
            _status = "Invalid WAV: unsupported channel configuration.";
            return false;
        }

        std::size_t const bytesPerSample = fmt.bitsPerSample / 8;
        if (bytesPerSample == 0) {
            _status = "Invalid WAV: zero bytes per sample.";
            return false;
        }

        std::size_t const totalSamples = audioData.size() / bytesPerSample;
        if (totalSamples == 0) {
            _status = "Audio file contains no samples.";
            return false;
        }

        std::size_t const frameCount = totalSamples / fmt.numChannels;
        std::vector<float> envelope(frameCount);
        auto const * pcm = reinterpret_cast<std::int16_t const *>(audioData.data());
        double const norm = 1.0 / (32768.0 * static_cast<double>(fmt.numChannels));
        float env = 0.0f;
        float const attack = 0.35f;
        float const release = 0.08f;

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            double accum = 0.0;
            for (std::uint16_t ch = 0; ch < fmt.numChannels; ++ch) {
                std::size_t idx = frame * fmt.numChannels + ch;
                if (idx >= totalSamples) {
                    break;
                }
                accum += static_cast<double>(pcm[idx]);
            }
            float mono = static_cast<float>(accum * norm);
            float magnitude = std::abs(mono);
            float const coeff = (magnitude > env) ? attack : release;
            env = std::lerp(env, magnitude, coeff);
            envelope[frame] = env;
        }

        _audioEnvelope = std::move(envelope);
        _sampleRate = static_cast<float>(fmt.sampleRate);
        _duration = (_sampleRate > 0.0f) ? static_cast<float>(_audioEnvelope.size()) / _sampleRate : 0.0f;
        _loaded = true;
        _currentLevel = 0.0f;
        _mockPlaybackTime = 0.0f;
        _pathW = resolved.wstring();

        std::ostringstream oss;
        oss << "Loaded WAV (" << fmt.numChannels << " ch, " << fmt.sampleRate << " Hz, "
            << std::fixed << std::setprecision(2) << _duration << " s)";
        _status = oss.str();
        return true;
    }

    float AudioInput::sampleEnvelope(float t) const {
        if (_audioEnvelope.empty() || _sampleRate <= 0.0f) {
            return 0.0f;
        }

        float const duration = (_duration > 0.0f) ? _duration : static_cast<float>(_audioEnvelope.size()) / _sampleRate;
        if (duration <= 0.0f) {
            return 0.0f;
        }

        float const minTime = 0.0f;
        float const maxTime = std::max(minTime, duration - (1.0f / _sampleRate));
        float const clamped = std::clamp(t, minTime, maxTime);
        float const samplePos = clamped * _sampleRate;
        std::size_t const i0 = static_cast<std::size_t>(samplePos);
        if (i0 >= _audioEnvelope.size() - 1) {
            return _audioEnvelope.back();
        }

        float const frac = samplePos - static_cast<float>(i0);
        return std::lerp(_audioEnvelope[i0], _audioEnvelope[i0 + 1], frac);
    }

    void AudioInput::Update(float deltaTime) {
        _baseGain = std::clamp(_baseGain, kMinBaseGain, kMaxBaseGain);

        _mockPlaybackTime += deltaTime;
        if (_loaded && _duration > 0.0f) {
            if (_loop) {
                _mockPlaybackTime = std::fmod(_mockPlaybackTime, _duration);
            } else {
                _mockPlaybackTime = std::min(_mockPlaybackTime, _duration);
            }
        }

        bool const haveAudio = _loaded && ! _audioEnvelope.empty() && _sampleRate > 0.0f;
        float modulation = 0.0f;
        if (haveAudio) {
            modulation = sampleEnvelope(_mockPlaybackTime);
        } else {
            float const speed = glm::max(_autoGainSpeed, 0.0f);
            _autoGainPhase += deltaTime * glm::max(0.1f, speed);
            float const twoPi = glm::pi<float>() * 2.0f;
            if (_autoGainPhase > twoPi) {
                _autoGainPhase = std::fmod(_autoGainPhase, twoPi);
            }
            modulation = 0.5f * (std::sin(_autoGainPhase) + 1.0f);
        }
        _currentLevel = modulation;

        if (! _autoGainEnabled) {
            _visualizationGain = _baseGain;
            return;
        }

        float const depth = glm::clamp(_autoGainDepth, 0.0f, 2.0f);
        float const scale = 1.0f + depth * modulation;
        _visualizationGain = glm::clamp(_baseGain * scale, kMinVizGain, kMaxVizGain);
    }

    void AudioInput::Clear() {
        stopPlayback();
        _audioEnvelope.clear();
        _loaded = false;
        _pathW.clear();
        _sampleRate = 0.0f;
        _duration = 0.0f;
        _currentLevel = 0.0f;
        _mockPlaybackTime = 0.0f;
        _autoGainPhase = 0.0f;
        _status = "No audio loaded.";
    }

    void AudioInput::SetLoop(bool loop) {
        if (_loop == loop) {
            return;
        }
        _loop = loop;
        if (_playing && _loaded) {
            startPlayback();
        }
    }

    bool AudioInput::LoopEnabled() const {
        return _loop;
    }

    void AudioInput::SetBaseGain(float value) {
        _baseGain = std::clamp(value, kMinBaseGain, kMaxBaseGain);
    }

    float AudioInput::BaseGain() const {
        return _baseGain;
    }

    void AudioInput::SetAutoGainEnabled(bool enabled) {
        _autoGainEnabled = enabled;
    }

    bool AudioInput::AutoGainEnabled() const {
        return _autoGainEnabled;
    }

    void AudioInput::SetAutoGainDepth(float depth) {
        _autoGainDepth = depth;
    }

    float AudioInput::AutoGainDepth() const {
        return _autoGainDepth;
    }

    void AudioInput::SetAutoGainSpeed(float speed) {
        _autoGainSpeed = speed;
    }

    float AudioInput::AutoGainSpeed() const {
        return _autoGainSpeed;
    }

    float AudioInput::CurrentLevel() const {
        return _currentLevel;
    }

    float AudioInput::VisualizationGain() const {
        return _visualizationGain;
    }

    float AudioInput::PlaybackTime() const {
        return _mockPlaybackTime;
    }

    float AudioInput::Duration() const {
        return _duration;
    }

    float AudioInput::PlaybackRatio() const {
        if (_duration <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(_mockPlaybackTime / _duration, 0.0f, 1.0f);
    }

    const std::string & AudioInput::Status() const {
        return _status;
    }

    void AudioInput::SetStatus(std::string message) {
        _status = std::move(message);
    }

    bool AudioInput::IsLoaded() const {
        return _loaded;
    }

    void AudioInput::RestartPlayback() {
        if (_loaded) {
            startPlayback();
        }
    }
} // namespace VCX::Apps::VolumeFX
