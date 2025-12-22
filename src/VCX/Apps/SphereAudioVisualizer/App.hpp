#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"
#include "Apps/SphereAudioVisualizer/GpuVolumeBuilder.hpp"
#include "Apps/SphereAudioVisualizer/AudioFilePlayer.hpp"
#include "kissfft/kiss_fft.h"
#include "Engine/Camera.hpp"
#include "Engine/GL/Program.h"
#include "Engine/GL/resource.hpp"
#include "Engine/GL/Texture.hpp"
#include "Engine/TextureND.hpp"
#include "Engine/app.h"
#include "Labs/Common/OrbitCameraManager.h"

namespace VCX::Apps::SphereAudioVisualizer {
    struct SparkParticleSystem;
    class App : public VCX::Engine::IApp {
    public:
        App();
        ~App();
        void OnFrame() override;

        enum class WindowType : int { Hann, Hamming };
        enum class MappingType : int { Linear, Log };
        enum class AggregateType : int { Average, Max };
        enum class TransferPreset : int {
            Smoke = 0,
            Neon,
            Heatmap,
        };

        struct AudioAnalysisSettings {
            int FftSizeIndex = 2; // 2048 by default
            WindowType Window = WindowType::Hann;
            MappingType Mapping = MappingType::Log;
            AggregateType Aggregate = AggregateType::Average;
            int NumBands = 16;
            float CompressK = 8.f;
            bool ShowSpectrum = false;
            bool AgcEnabled = true;
            float AgcTarget = 0.8f;
            float AgcAttack = 0.08f;   // seconds
            float AgcRelease = 0.4f;   // seconds
            float AgcMaxGain = 20.f;
            float MinFrequency = 20.f;
        };

        struct AudioAnalysisState {
            std::vector<float> Window;
            std::vector<float> WindowCoeffs;
            std::vector<float> Spectrum; // magnitude per bin (0..Nyquist)
            std::vector<float> SpectrumDownsample;
            std::vector<float> BandEnergies;
            std::vector<kiss_fft_cpx> FftIn;
            std::vector<kiss_fft_cpx> FftOut;
            WindowType CachedWindow = WindowType::Hann;
            int CachedWindowSize = 0;
            float AgcGain = 1.f;
            float LastFftMs = 0.f;
            float EnergyMin = 0.f;
            float EnergyMax = 0.f;
            float EnergyAvg = 0.f;
            std::size_t Underruns = 0;
        };

        static constexpr std::array<int, 4> kFftSizes { 512, 1024, 2048, 4096 };

        struct SparkSettings {
            bool Enable = true;
            int MaxParticles = 30000;
            float SpawnRateBase = 120.f;
            float SpawnRateBass = 360.f;
            float Speed = 2.4f;
            float Size = 0.04f;
            float Streak = 1.5f;
            float Drag = 0.8f;
            float ColorWarmth = 0.85f;
        };

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

        enum class BackgroundMode : int {
            Gradient = 0,
            Starfield,
            Nebula,
        };

        struct BackgroundSettings {
            bool Enable = true;
            BackgroundMode Mode = BackgroundMode::Gradient;
            float Intensity = 0.9f;
            float Speed = 1.0f;
            glm::vec3 ColorA = glm::vec3(0.04f, 0.04f, 0.1f);
            glm::vec3 ColorB = glm::vec3(0.25f, 0.12f, 0.45f);
        };

        enum class ToneMappingMode : int {
            Reinhard = 0,
            ACES,
        };

        struct ToneMappingSettings {
            float Exposure = 1.1f;
            ToneMappingMode Mode = ToneMappingMode::Reinhard;
        };

        struct BloomSettings {
            bool Enable = true;
            float Threshold = 1.f;
            float Knee = 0.5f;
            float Strength = 0.6f;
            float BlurRadius = 1.5f;
        };

        struct StatsSnapshot {
            float AvgSteps       = 0.f;
            float EarlyExitRatio = 0.f;
        };

        enum class PerturbMode : int {
            Ripple = 0,
            Noise  = 1,
        };

        struct DynamicSettings {
            float NoiseStrength = 0.06f;
            float NoiseFreq     = 4.f;
            float NoiseSpeed    = 0.8f;
            float RippleAmp     = 0.08f;
            float RippleFreq    = 16.f;
            float RippleSpeed   = 1.4f;
            PerturbMode Mode    = PerturbMode::Ripple;
        };

        struct TransferControlPoint {
            float Position = 0.f;
            glm::vec3 Color = glm::vec3(0.4f);
            float Alpha = 1.f;
        };

        struct TransferFunctionSettings {
            float LowThreshold = 0.f;
            float HighThreshold = 1.f;
            float Gamma = 1.f;
            float OverallAlpha = 1.f;
            std::array<TransferControlPoint, 4> ControlPoints {
                TransferControlPoint{0.f, glm::vec3(0.05f), 0.05f},
                TransferControlPoint{0.35f, glm::vec3(0.2f, 0.25f, 0.3f), 0.3f},
                TransferControlPoint{0.7f, glm::vec3(0.6f, 0.4f, 0.2f), 0.7f},
                TransferControlPoint{1.f, glm::vec3(0.95f, 0.6f, 0.2f), 1.f},
            };
        };

        void RenderVolume(float deltaTime);
        void RenderBackground(float deltaTime);
        void RenderToneMappedResult(glm::ivec2 const & size);
        bool EnsureHdrFramebuffer(glm::ivec2 const & size);
        bool EnsureBloomResources(glm::ivec2 const & size);
        void RenderBloomPasses(glm::ivec2 const & size);
        void UpdateSparks(float deltaTime);
        void RenderSparks(glm::ivec2 const & size);
        void ResetStatsBuffer();
        void LogDynamicParam(char const * name, float value);
        void RenderAudioUI();
        void UpdateAudioAnalysis(float deltaTime);
        void RenderTransferFunctionUI();
        void UpdateTransferFunctionTexture();
        void ApplyTransferPreset(TransferPreset preset);
        glm::vec4 EvaluateTransferFunction(float sample) const;
        static char const * ColorModeName(ColorMode mode);
        static bool TryParseColorMode(std::string const & value, ColorMode & out);
        static char const * BackgroundModeName(BackgroundMode mode);
        static char const * ToneMappingModeName(ToneMappingMode mode);
        void LoadConfig();
        void SaveConfig();
        std::filesystem::path ConfigFilePath() const;
        void InitGLCapabilities();

        float _alpha;
        SphereVolumeData _volumeData;
        GpuVolumeBuilder _gpuVolumeBuilder;
        AudioFilePlayer _audio;
        char _audioPath[512] = "";
        bool _audioLoop = false;
        bool _monoMixMode = true;
        AudioAnalysisSettings _analysisSettings;
        AudioAnalysisState _analysisState;
        kiss_fft_cfg _fftCfg = nullptr;
        TransferFunctionSettings _transferSettings;
        TransferPreset _transferPreset = TransferPreset::Smoke;
        bool _transferDirty = true;
        int _fftSize = kFftSizes[2];
        static constexpr std::size_t kOscilloscopeSamples = 256;
        int _audioHeadroom = kFftSizes.back();
        std::array<float, kOscilloscopeSamples> _oscilloscopePoints{};
        float _audioWindowRms = 0.f;
        float _audioLogTimer = 0.f;
        float _fftLogTimer = 0.f;
        float _volumeLogTimer = 0.f;
        float _audioBass = 0.f;
        float _audioTreble = 0.f;
        float _volumeBuildMs = 0.f;
        float _volumeUploadMs = 0.f;
        float _gpuBuildMs = 0.f;
        float _renderMs = 0.f;
        std::size_t _fftUpdatesPerSecond = 0;
        std::size_t _fftUpdateCounter = 0;
        std::size_t _audioReadable = 0;
        VCX::Engine::GL::UniqueProgram _backgroundProgram;
        VCX::Engine::GL::UniqueProgram _volumeProgram;
        VCX::Engine::GL::UniqueProgram _tonemapProgram;
        VCX::Engine::GL::UniqueProgram _bloomBrightProgram;
        VCX::Engine::GL::UniqueProgram _bloomBlurProgram;
        VCX::Engine::GL::UniqueProgram _sparkProgram;
        VCX::Engine::GL::UniqueVertexArray _fullscreenVAO;
        VCX::Engine::GL::UniqueArrayBuffer _fullscreenVBO;
        VCX::Engine::GL::UniqueVertexArray _sparkVAO;
        VCX::Engine::GL::UniqueArrayBuffer _sparkQuadVBO;
        VCX::Engine::GL::UniqueArrayBuffer _sparkInstanceVBO;
        VCX::Engine::Camera _camera;
        VCX::Labs::Common::OrbitCameraManager _cameraManager;
        VCX::Engine::GL::UniqueTexture2D _transferLutTexture;
        RenderSettings _renderSettings;
        DynamicSettings _dynamicSettings;
        SparkSettings _sparkSettings;
        BackgroundSettings _backgroundSettings;
        ToneMappingSettings _toneMappingSettings;
        BloomSettings _bloomSettings;
        StatsSnapshot _statsSnapshot;
        VCX::Engine::GL::UniqueFramebuffer _hdrFbo;
        VCX::Engine::GL::UniqueRenderbuffer _hdrDepth;
        VCX::Engine::GL::UniqueTexture2D _hdrColor;
        VCX::Engine::GL::UniqueFramebuffer _bloomBrightFbo;
        VCX::Engine::GL::UniqueFramebuffer _bloomTempFbo;
        VCX::Engine::GL::UniqueTexture2D _bloomBrightTexture;
        VCX::Engine::GL::UniqueTexture2D _bloomTempTexture;
        std::unique_ptr<SparkParticleSystem> _sparkSystem;
        glm::ivec2 _hdrSize { 0, 0 };
        glm::ivec2 _bloomSize { 0, 0 };
        bool _hdrFramebufferValid = false;
        bool _bloomResourcesValid = false;
        GLuint _statsBuffer = 0;
        GLuint _bloomTimeQuery = 0;
        float _bloomMs = 0.f;
        float _statsTimer = 0.f;
        uint64_t _accumulatedSteps = 0;
        uint64_t _accumulatedRays = 0;
        uint64_t _accumulatedEarly = 0;
        uint32_t _frameIndex = 0;
        float _time = 0.f;
        bool _computeSupported = false;
        bool _forceCpuBuild = false;
        bool _useGpuBuild = true;
        bool _buildOnEnergyUpdate = true;
        bool _energiesUpdatedThisFrame = false;
        uint32_t _lastBuildFrameIndex = 0;
    };

    void EnsureLogger();
    int  RunApp();
} // namespace VCX::Apps::SphereAudioVisualizer
