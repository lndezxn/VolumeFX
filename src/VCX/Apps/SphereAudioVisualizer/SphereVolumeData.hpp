#pragma once

#include <cstddef>
#include <cstdint>

#include <imgui.h>

#include "Engine/GL/Texture.hpp"
#include "Engine/TextureND.hpp"

namespace VCX::Apps::SphereAudioVisualizer {
    class SphereVolumeData {
    public:
        struct Settings {
            std::size_t VolumeSize = 96;
            int NumShells = 8;
            float Thickness = 0.08f;
            float GlobalGain = 1.f;
        };

        SphereVolumeData();

        Settings const & GetSettings() const;
        void SetSettings(Settings settings);
        void Regenerate();

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

        void BuildVolume();
        void UploadVolumeTexture();
        void UpdateSliceTexture();

        static float ComputeLayerAmplitude(int layer, int shellCount);
    };
}
