#pragma once

#include <string>
#include <vector>

namespace VCX::Apps::VolumeFX {
    class AudioInput {
    public:
        AudioInput();
        ~AudioInput();

        bool LoadFromPath(const char * path);
        void Clear();
        void Update(float deltaTime);

        void SetLoop(bool loop);
        bool LoopEnabled() const;

        void SetBaseGain(float value);
        float BaseGain() const;

        void SetAutoGainEnabled(bool enabled);
        bool AutoGainEnabled() const;

        void SetAutoGainDepth(float depth);
        float AutoGainDepth() const;

        void SetAutoGainSpeed(float speed);
        float AutoGainSpeed() const;

        float CurrentLevel() const;
        float VisualizationGain() const;

        float PlaybackTime() const;
        float Duration() const;
        float PlaybackRatio() const;

        const std::string & Status() const;
        void SetStatus(std::string message);

        bool IsLoaded() const;

        void RestartPlayback();

    private:
        void stopPlayback();
        void startPlayback();
        bool loadAudioFile(const char * path);
        float sampleEnvelope(float t) const;

        std::vector<float> _audioEnvelope;
        std::string        _status;
        bool               _loaded = false;
        bool               _loop = true;
        bool               _playing = false;
        std::wstring       _pathW;
        float              _sampleRate = 0.0f;
        float              _duration = 0.0f;
        float              _currentLevel = 0.0f;
        float              _visualizationGain = 1.0f;
        float              _baseGain = 1.0f;
        bool               _autoGainEnabled = true;
        float              _autoGainDepth = 0.35f;
        float              _autoGainSpeed = 1.2f;
        float              _autoGainPhase = 0.0f;
        float              _mockPlaybackTime = 0.0f;
    };
} // namespace VCX::Apps::VolumeFX
