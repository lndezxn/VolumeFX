#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <miniaudio.h>

namespace VCX::Apps::SphereAudioVisualizer {
    class AudioFilePlayer {
    public:
        AudioFilePlayer();
        ~AudioFilePlayer();

        bool LoadFile(std::string const & path);
        void Play();
        void Pause();
        void Stop();
        void SetLoop(bool loop);

        bool IsLoaded() const;
        bool IsPlaying() const;
        bool IsLooping() const;
        bool UsingSineFallback() const;

        float GetTimeSeconds() const;
        float GetDurationSeconds() const;
        std::uint32_t GetSampleRate() const;
        std::uint32_t GetChannels() const;
        float GetRingFillRatio() const;
        std::size_t GetAvailableSamples() const;
        std::uint64_t GetOverrunWrites() const { return _overrunWrites.load(); }
        std::uint64_t GetDroppedSamples() const { return _droppedSamples.load(); }
        std::uint64_t GetUnderrunReads() const { return _underrunReads.load(); }

        /**
         * Drain up to maxSamples mono samples from the analysis ring buffer.
         * @return number of samples actually read.
         */
        std::size_t ReadSamples(float * dst, std::size_t maxSamples);

        /**
         * Copy the latest fftSize samples without advancing the read cursor (peek).
         * If available samples < fftSize, the tail is zero padded.
         * headroom keeps up to (fftSize+headroom) recent samples; older ones are skipped logically.
         */
        std::size_t GetLatestWindow(float * dst, std::size_t fftSize, std::size_t headroom);

        std::string const & GetLastError() const;

    private:
        static void DataCallback(ma_device * device, void * output, void const * input, ma_uint32 frameCount);
        void HandleCallback(float * output, ma_uint32 frameCount);
        bool StartDevice();
        void StopDevice();
        void ResetRing(std::uint32_t sampleRate);
        void WriteRing(const float * samples, std::size_t frames);
        std::size_t ReadRing(float * dst, std::size_t maxSamples);
        std::size_t RingReadable() const;
        std::size_t RingCapacity() const;
        void DiscardSamples(std::size_t count);
        void ResetDecoderState();

        ma_decoder   _decoder{};
        ma_device    _device{};
        bool         _decoderInit = false;
        bool         _deviceInit  = false;

        std::string  _path;
        std::string  _lastError;
        std::atomic<bool> _loop{false};
        std::atomic<bool> _paused{true};
        std::atomic<bool> _useSine{true};
        std::atomic<bool> _loaded{false};

        std::uint32_t _sampleRate    = 48000;
        std::uint32_t _channels      = 2;
        std::uint64_t _totalFrames   = 0;
        std::atomic<std::uint64_t> _cursorFrames{0};

        std::atomic<std::uint64_t> _overrunWrites{0};
        std::atomic<std::uint64_t> _droppedSamples{0};
        std::atomic<std::uint64_t> _underrunReads{0};

        float         _sinePhase   = 0.f;
        std::vector<float> _scratch;
        std::vector<float> _ring;
        std::size_t    _ringCapacity = 0;
        std::atomic<std::size_t> _ringWrite{0};
        std::atomic<std::size_t> _ringRead{0};
        std::mutex    _mutex; // protects decoder seek/reset during load/stop

        static constexpr float kTau = 6.28318530718f;
    };
}
