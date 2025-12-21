#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"
#include "Apps/SphereAudioVisualizer/AudioFilePlayer.hpp"
#include "Engine/Camera.hpp"
#include "Engine/GL/Program.h"
#include "Engine/GL/resource.hpp"
#include "Engine/app.h"
#include "Labs/Common/OrbitCameraManager.h"

namespace VCX::Apps::SphereAudioVisualizer {
    class App : public VCX::Engine::IApp {
    public:
        App();
        ~App();
        void OnFrame() override;

    private:
        enum class ColorMode : int {
            Gray,
            Gradient,
        };

        struct RenderSettings {
            float StepSize    = 0.01f;
            int   MaxSteps    = 256;
            float AlphaScale  = 1.f;
            ColorMode Mode    = ColorMode::Gradient;
            bool  EnableJitter = true;
        };

        struct StatsSnapshot {
            float AvgSteps       = 0.f;
            float EarlyExitRatio = 0.f;
        };

        struct DynamicSettings {
            float NoiseStrength = 0.06f;
            float NoiseFreq     = 4.f;
            float NoiseSpeed    = 0.8f;
            float RippleAmp     = 0.08f;
            float RippleFreq    = 16.f;
            float RippleSpeed   = 1.4f;
        };

        void RenderVolume(float deltaTime);
        void ResetStatsBuffer();
        void LogDynamicParam(char const * name, float value);
        void RenderAudioUI();
        void UpdateAudioAnalysis(float deltaTime);

        float _alpha;
        SphereVolumeData _volumeData;
        AudioFilePlayer _audio;
        char _audioPath[512] = "";
        bool _audioLoop = false;
        std::vector<float> _audioWindow;
        static constexpr std::size_t kFftWindowSize = 2048;
        static constexpr std::size_t kOscilloscopeSamples = 256;
        int _audioHeadroom = static_cast<int>(kFftWindowSize * 2);
        std::array<float, kOscilloscopeSamples> _oscilloscopePoints{};
        float _audioWindowRms = 0.f;
        float _audioLogTimer = 0.f;
        std::size_t _fftUpdatesPerSecond = 0;
        std::size_t _fftUpdateCounter = 0;
        std::size_t _audioReadable = 0;
        VCX::Engine::GL::UniqueProgram _volumeProgram;
        VCX::Engine::GL::UniqueVertexArray _fullscreenVAO;
        VCX::Engine::GL::UniqueArrayBuffer _fullscreenVBO;
        VCX::Engine::Camera _camera;
        VCX::Labs::Common::OrbitCameraManager _cameraManager;
        RenderSettings _renderSettings;
        DynamicSettings _dynamicSettings;
        StatsSnapshot _statsSnapshot;
        GLuint _statsBuffer = 0;
        float _statsTimer = 0.f;
        uint64_t _accumulatedSteps = 0;
        uint64_t _accumulatedRays = 0;
        uint64_t _accumulatedEarly = 0;
        uint32_t _frameIndex = 0;
        float _time = 0.f;
    };

    void EnsureLogger();
    int  RunApp();
} // namespace VCX::Apps::SphereAudioVisualizer
