#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "Apps/SphereAudioVisualizer/AudioFilePlayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr std::uint32_t kDefaultSampleRate = 48000;
        constexpr std::uint32_t kDefaultChannels   = 2;
        constexpr std::uint32_t kRingSeconds       = 4; // ring holds ~4 seconds of mono audio
    }

    AudioFilePlayer::AudioFilePlayer() {
        ResetRing(_sampleRate);
        StartDevice();
    }

    AudioFilePlayer::~AudioFilePlayer() {
        StopDevice();
        if (_decoderInit) {
            ma_decoder_uninit(&_decoder);
        }
        _ring.clear();
    }

    bool AudioFilePlayer::IsLoaded() const { return _loaded.load(); }
    bool AudioFilePlayer::IsPlaying() const { return !_paused.load(); }
    bool AudioFilePlayer::IsLooping() const { return _loop.load(); }
    bool AudioFilePlayer::UsingSineFallback() const { return _useSine.load(); }

    std::uint32_t AudioFilePlayer::GetSampleRate() const { return _sampleRate; }
    std::uint32_t AudioFilePlayer::GetChannels() const { return _channels; }
    float AudioFilePlayer::GetDurationSeconds() const { return _totalFrames == 0 ? 0.f : float(_totalFrames) / float(_sampleRate); }
    float AudioFilePlayer::GetTimeSeconds() const { return float(_cursorFrames.load()) / float(_sampleRate); }

    std::string const & AudioFilePlayer::GetLastError() const { return _lastError; }

    float AudioFilePlayer::GetRingFillRatio() const {
        std::size_t readable = RingReadable();
        std::size_t capacity = RingCapacity();
        if (capacity == 0) return 0.f;
        return float(readable) / float(capacity);
    }

    std::size_t AudioFilePlayer::ReadSamples(float * dst, std::size_t maxSamples) {
        if (maxSamples == 0 || dst == nullptr) return 0;
        return ReadRing(dst, maxSamples);
    }

    std::size_t AudioFilePlayer::GetAvailableSamples() const {
        return RingReadable();
    }

    void AudioFilePlayer::ResetRing(std::uint32_t sampleRate) {
        _ringCapacity = std::max<std::size_t>(sampleRate * kRingSeconds, std::size_t(1024));
        _ring.assign(_ringCapacity, 0.f);
        _ringWrite.store(0);
        _ringRead.store(0);
        _overrunWrites.store(0);
        _droppedSamples.store(0);
        _underrunReads.store(0);
    }

    void AudioFilePlayer::ResetDecoderState() {
        _cursorFrames.store(0);
        _sinePhase = 0.f;
        if (_decoderInit) {
            ma_decoder_seek_to_pcm_frame(&_decoder, 0);
        }
    }

    bool AudioFilePlayer::StartDevice() {
        StopDevice();
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format   = ma_format_f32;
        config.playback.channels = _channels;
        config.sampleRate        = _sampleRate;
        config.dataCallback      = &AudioFilePlayer::DataCallback;
        config.pUserData         = this;

        ma_result res = ma_device_init(nullptr, &config, &_device);
        if (res != MA_SUCCESS) {
            _lastError = "ma_device_init failed";
            _deviceInit = false;
            return false;
        }
        res = ma_device_start(&_device);
        if (res != MA_SUCCESS) {
            ma_device_uninit(&_device);
            _lastError = "ma_device_start failed";
            _deviceInit = false;
            return false;
        }
        _deviceInit = true;
        return true;
    }

    void AudioFilePlayer::StopDevice() {
        if (_deviceInit) {
            ma_device_uninit(&_device);
            _deviceInit = false;
        }
    }

    bool AudioFilePlayer::LoadFile(std::string const & path) {
        std::scoped_lock lock(_mutex);
        _path = path;
        _lastError.clear();

        if (_deviceInit) {
            ma_device_stop(&_device);
        }

        if (_decoderInit) {
            ma_decoder_uninit(&_decoder);
            _decoderInit = false;
        }

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_result res = ma_decoder_init_file(path.c_str(), &cfg, &_decoder);
        if (res != MA_SUCCESS) {
            _lastError = "Failed to load audio: code " + std::to_string(res);
            spdlog::error("Audio load failed for {} (code {})", path, static_cast<int>(res));
            _useSine.store(true);
            _loaded.store(false);
            _sampleRate = kDefaultSampleRate;
            _channels   = kDefaultChannels;
            _totalFrames = 0;
            ResetRing(_sampleRate);
            ResetDecoderState();
            StartDevice();
            return false;
        }

        _decoderInit  = true;
        _sampleRate   = _decoder.outputSampleRate;
        _channels     = _decoder.outputChannels;
        ma_decoder_get_length_in_pcm_frames(&_decoder, &_totalFrames);
        ResetRing(_sampleRate);
        ResetDecoderState();
        _useSine.store(false);
        _loaded.store(true);
        _paused.store(true);

        bool deviceOk = StartDevice();
        if (!deviceOk) {
            spdlog::error("Audio device start failed after load {}", path);
            _useSine.store(true);
            _loaded.store(false);
        } else {
            spdlog::info("Audio loaded: {} ({} Hz, {} ch, {:.2f}s)", path, _sampleRate, _channels, GetDurationSeconds());
        }
        return deviceOk;
    }

    void AudioFilePlayer::Play() {
        if (!_deviceInit) {
            if (!StartDevice()) return;
        }
        _paused.store(false);
    }

    void AudioFilePlayer::Pause() {
        _paused.store(true);
    }

    void AudioFilePlayer::Stop() {
        std::scoped_lock lock(_mutex);
        _paused.store(true);
        ResetDecoderState();
    }

    void AudioFilePlayer::SetLoop(bool loop) {
        _loop.store(loop);
    }

    void AudioFilePlayer::DataCallback(ma_device * device, void * output, void const *, ma_uint32 frameCount) {
        auto * self = reinterpret_cast<AudioFilePlayer *>(device->pUserData);
        if (self) {
            self->HandleCallback(reinterpret_cast<float *>(output), frameCount);
        }
    }

    void AudioFilePlayer::HandleCallback(float * output, ma_uint32 frameCount) {
        if (output == nullptr) return;
        const ma_uint32 channels = _channels;
        std::fill(output, output + std::size_t(frameCount) * channels, 0.f);

        if (_paused.load()) {
            return;
        }

        if (_useSine.load() || !_decoderInit) {
            // Sine fallback
            float freq = 220.f;
            float phase = _sinePhase;
            for (ma_uint32 i = 0; i < frameCount; ++i) {
                float sample = std::sin(phase);
                phase += kTau * freq / float(_sampleRate);
                if (phase > kTau) phase -= kTau;
                for (ma_uint32 c = 0; c < channels; ++c) {
                    output[i * channels + c] = sample;
                }
            }
            _sinePhase = phase;

            _scratch.resize(frameCount);
            for (ma_uint32 i = 0; i < frameCount; ++i) {
                _scratch[i] = output[i * channels];
            }
            WriteRing(_scratch.data(), frameCount);
            _cursorFrames.fetch_add(frameCount, std::memory_order_relaxed);
            return;
        }

        ma_uint32 framesRemaining = frameCount;
        float *   outPtr = output;
        while (framesRemaining > 0) {
            ma_uint64 framesRead64 = 0;
            ma_result res = ma_decoder_read_pcm_frames(&_decoder, outPtr, framesRemaining, &framesRead64);
            ma_uint32 framesRead = static_cast<ma_uint32>(framesRead64);
            if (res != MA_SUCCESS && framesRead == 0) {
                // Decoder error; fallback to silence and pause.
                _paused.store(true);
                break;
            }

            if (framesRead == 0) {
                if (_loop.load()) {
                    ma_decoder_seek_to_pcm_frame(&_decoder, 0);
                    _cursorFrames.store(0);
                    continue;
                } else {
                    _paused.store(true);
                    break;
                }
            }

            // Write mono samples to ring buffer
            _scratch.resize(framesRead);
            for (ma_uint32 i = 0; i < framesRead; ++i) {
                float accum = 0.f;
                for (ma_uint32 c = 0; c < channels; ++c) {
                    accum += outPtr[i * channels + c];
                }
                _scratch[i] = accum / float(channels);
            }
            WriteRing(_scratch.data(), framesRead);

            outPtr += std::size_t(framesRead) * channels;
            framesRemaining -= framesRead;
            _cursorFrames.fetch_add(framesRead, std::memory_order_relaxed);
        }
    }

    std::size_t AudioFilePlayer::RingCapacity() const {
        return _ringCapacity;
    }

    std::size_t AudioFilePlayer::RingReadable() const {
        std::size_t write = _ringWrite.load(std::memory_order_acquire);
        std::size_t read  = _ringRead.load(std::memory_order_acquire);
        return write - read;
    }

    void AudioFilePlayer::WriteRing(const float * samples, std::size_t frames) {
        if (_ringCapacity == 0 || frames == 0) return;
        std::size_t write = _ringWrite.load(std::memory_order_relaxed);
        std::size_t read  = _ringRead.load(std::memory_order_acquire);
        std::size_t available = write - read;
        std::size_t freeSpace = _ringCapacity - available;
        std::size_t toWrite = std::min(frames, _ringCapacity);

        if (toWrite > freeSpace) {
            std::size_t drop = toWrite - freeSpace; // overwrite-old strategy
            read += drop;
            _ringRead.store(read, std::memory_order_release);
            _overrunWrites.fetch_add(1, std::memory_order_relaxed);
            _droppedSamples.fetch_add(drop, std::memory_order_relaxed);
        }

        std::size_t start = frames > toWrite ? frames - toWrite : 0;
        for (std::size_t i = 0; i < toWrite; ++i) {
            _ring[(write + i) % _ringCapacity] = samples[start + i];
        }
        _ringWrite.store(write + toWrite, std::memory_order_release);
    }

    std::size_t AudioFilePlayer::ReadRing(float * dst, std::size_t maxSamples) {
        if (_ringCapacity == 0 || maxSamples == 0) return 0;
        std::size_t write = _ringWrite.load(std::memory_order_acquire);
        std::size_t read  = _ringRead.load(std::memory_order_relaxed);
        std::size_t available = write - read;
        std::size_t toRead = std::min(maxSamples, available);
        for (std::size_t i = 0; i < toRead; ++i) {
            dst[i] = _ring[(read + i) % _ringCapacity];
        }
        _ringRead.store(read + toRead, std::memory_order_release);
        return toRead;
    }

    std::size_t AudioFilePlayer::GetLatestWindow(float * dst, std::size_t fftSize, std::size_t headroom) {
        if (dst == nullptr || fftSize == 0 || _ringCapacity == 0) return 0;
        std::size_t write = _ringWrite.load(std::memory_order_acquire);
        std::size_t read  = _ringRead.load(std::memory_order_acquire);
        std::size_t available = write - read;
        if (available > _ringCapacity) available = _ringCapacity; // clamp in case of overwrite

        std::size_t target = fftSize + headroom;
        if (target > _ringCapacity) target = _ringCapacity;
        if (available > target) available = target;

        std::size_t toCopy = std::min<std::size_t>(fftSize, available);
        if (toCopy > 0) {
            // Copy the latest toCopy samples ending at write-1.
            std::size_t writePos = write % _ringCapacity;
            std::size_t start = (write >= toCopy)
                ? ((write - toCopy) % _ringCapacity)
                : ((_ringCapacity + writePos + _ringCapacity - (toCopy % _ringCapacity)) % _ringCapacity);

            std::size_t firstChunk = std::min(toCopy, _ringCapacity - start);
            std::memcpy(dst, _ring.data() + start, firstChunk * sizeof(float));
            if (firstChunk < toCopy) {
                std::memcpy(dst + firstChunk, _ring.data(), (toCopy - firstChunk) * sizeof(float));
            }
        }

        if (toCopy < fftSize) {
            std::fill(dst + toCopy, dst + fftSize, 0.f);
            _underrunReads.fetch_add(fftSize - toCopy, std::memory_order_relaxed);
        }
        return toCopy;
    }

    void AudioFilePlayer::DiscardSamples(std::size_t count) {
        if (count == 0) return;
        std::size_t available = RingReadable();
        std::size_t toDiscard = std::min(count, available);
        if (toDiscard == 0) return;
        std::size_t read = _ringRead.load(std::memory_order_relaxed);
        _ringRead.store(read + toDiscard, std::memory_order_release);
    }
}
