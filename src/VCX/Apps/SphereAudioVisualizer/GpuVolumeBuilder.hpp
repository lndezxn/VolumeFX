#pragma once

#include <cstddef>
#include <vector>

#include <glad/glad.h>

#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"
#include "Engine/GL/Program.h"

namespace VCX::Apps::SphereAudioVisualizer {
    class GpuVolumeBuilder {
    public:
        struct BuildStats {
            float BuildMs = 0.f;
            float UploadMs = 0.f;
        };

        GpuVolumeBuilder();
        ~GpuVolumeBuilder();

        void EnsureResources(std::size_t volumeSize);
        BuildStats DispatchBuild(std::vector<float> const & energies, SphereVolumeData::Settings const & settings);
        GLuint GetVolumeTexture() const;
        float GetLastBuildMs() const;

    private:
        static constexpr std::size_t kMinVolumeSize = 32;
        static constexpr std::size_t kMaxVolumeSize = 256;
        static constexpr std::size_t kMaxBands = 256;
        static constexpr float kMinThickness = 0.01f;
        static constexpr float kMaxAmpScale = 5.f;
        static constexpr float kMinGlobalGain = 0.1f;
        static constexpr float kMaxGlobalGain = 5.f;
        static constexpr float kMinSmoothing = 0.f;
        static constexpr float kMaxSmoothing = 1.f;
        static constexpr float kMinRadius = 0.05f;
        static constexpr float kMaxRadius = 1.f;

        void UpdateBandTables(std::size_t bandCount, SphereVolumeData::Settings const & settings);
        void UpdateSmoothedEnergies(std::vector<float> const & energies, float smoothingFactor);
        void EnsureTextureAllocated(std::size_t size);
        float ComputeBandGain(std::size_t bandIndex, std::size_t bandCount, float tilt) const;

        std::size_t _volumeSize = 0;
        SphereVolumeData::RadiusDistribution _radiusLayout = SphereVolumeData::RadiusDistribution::Linear;
        VCX::Engine::GL::UniqueProgram _computeProgram;
        VCX::Engine::GL::UniqueTexture3D _volumeTexture;
        GLuint _timeQuery = 0;
        std::size_t _bandCount = 0;
        std::vector<float> _bandBaseRadius;
        std::vector<float> _bandGains;
        std::vector<float> _smoothedEnergies;
        float _lastBuildMs = 0.f;
    };
} // namespace VCX::Apps::SphereAudioVisualizer
