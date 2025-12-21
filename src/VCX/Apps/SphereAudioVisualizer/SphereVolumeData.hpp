#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "Engine/GL/Texture.hpp"
#include "Engine/TextureND.hpp"

namespace VCX::Apps::SphereAudioVisualizer {
    class SphereVolumeData {
    public:
        enum class RadiusDistribution {
            Linear,
            Log,
        };

        struct Settings {
            std::size_t VolumeSize = 96;
            float AmpScale = 0.6f;
            float ThicknessScale = 1.f;
            float BaseThickness = 0.08f;
            float GlobalGain = 1.f;
            float SmoothingFactor = 0.2f;
            float Tilt = 0.f;
            RadiusDistribution RadiusLayout = RadiusDistribution::Linear;
        };

        struct BuildStats {
            float BuildMs = 0.f;
            float UploadMs = 0.f;
        };

        SphereVolumeData();

        Settings const & GetSettings() const;
        void SetSettings(Settings settings);
        void Regenerate();
        BuildStats UpdateVolume(std::vector<float> const & energies);

        void SetSliceIndex(std::size_t index);
        std::size_t GetSliceIndex() const;
        std::size_t GetVolumeSize() const;

        ImTextureID GetSliceTextureHandle() const;
        GLuint GetVolumeTextureId() const;

    private:
        Settings                                     _settings;
        Engine::Texture3D<Engine::Formats::R8>      _volume;
        VCX::Engine::GL::UniqueTexture3D             _volumeTexture;
        VCX::Engine::GL::UniqueTexture2D             _sliceTexture;
        std::size_t                                  _sliceIndex = 0;
        std::size_t                                  _bandCount = 0;
        std::vector<float>                           _bandBaseRadius;
        std::vector<float>                           _bandGains;
        std::vector<float>                           _smoothedEnergies;

        void BuildVolume(std::vector<float> const & energies);
        void UploadVolumeTexture();
        void UpdateSliceTexture();

        void EnsureBandTables(std::size_t bandCount);
        float ComputeBandGain(std::size_t bandIndex, std::size_t bandCount) const;
    };
}
