#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"

#include <algorithm>
#include <cmath>

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr std::size_t kMinVolumeSize = 32;
        constexpr std::size_t kMaxVolumeSize = 256;
        constexpr int         kMinShells     = 1;
        constexpr int         kMaxShells     = 32;

        inline VCX::Engine::GL::SamplerOptions MakeSamplerOptions() {
            return VCX::Engine::GL::SamplerOptions {
                .WrapU     = VCX::Engine::GL::WrapMode::Clamp,
                .WrapV     = VCX::Engine::GL::WrapMode::Clamp,
                .WrapW     = VCX::Engine::GL::WrapMode::Clamp,
                .MinFilter = VCX::Engine::GL::FilterMode::Linear,
                .MagFilter = VCX::Engine::GL::FilterMode::Linear,
            };
        }

        float ComputeShellAmplitude(int layer, int shellCount) {
            if (shellCount <= 1)
                return 1.f;
            return 0.4f + 0.6f * float(layer) / float(shellCount - 1);
        }
    }

    SphereVolumeData::SphereVolumeData() {
        Regenerate();
    }

    SphereVolumeData::Settings const & SphereVolumeData::GetSettings() const {
        return _settings;
    }

    void SphereVolumeData::SetSettings(Settings settings) {
        settings.VolumeSize = std::clamp(settings.VolumeSize, kMinVolumeSize, kMaxVolumeSize);
        settings.NumShells = std::clamp(settings.NumShells, kMinShells, kMaxShells);
        settings.Thickness = std::clamp(settings.Thickness, 0.02f, 0.5f);
        settings.GlobalGain = std::clamp(settings.GlobalGain, 0.1f, 5.f);
        settings.StaticRippleAmp = std::clamp(settings.StaticRippleAmp, 0.f, 0.3f);
        settings.StaticRippleFreq = std::clamp(settings.StaticRippleFreq, 0.1f, 60.f);
        _settings = settings;
    }

    void SphereVolumeData::Regenerate() {
        if (_settings.VolumeSize == 0) {
            _settings.VolumeSize = kMinVolumeSize;
        }
        BuildVolume();
        UploadVolumeTexture();
        _sliceIndex = std::min(_sliceIndex, std::max<std::size_t>(1, _settings.VolumeSize) - 1);
        UpdateSliceTexture();
    }

    void SphereVolumeData::SetSliceIndex(std::size_t index) {
        if (_settings.VolumeSize == 0) {
            _sliceIndex = 0;
            return;
        }
        auto const maxSlice = _settings.VolumeSize - 1;
        _sliceIndex = std::clamp(index, std::size_t{0}, maxSlice);
        UpdateSliceTexture();
    }

    std::size_t SphereVolumeData::GetSliceIndex() const {
        return _sliceIndex;
    }

    std::size_t SphereVolumeData::GetVolumeSize() const {
        return _settings.VolumeSize;
    }

    ImTextureID SphereVolumeData::GetSliceTextureHandle() const {
        return reinterpret_cast<ImTextureID>(std::uintptr_t(_sliceTexture.Get()));
    }

    GLuint SphereVolumeData::GetVolumeTextureId() const {
        return _volumeTexture.Get();
    }

    void SphereVolumeData::BuildVolume() {
        if (_settings.VolumeSize == 0) {
            return;
        }
        auto const size = _settings.VolumeSize;
        auto const shellCount = std::max(_settings.NumShells, 1);
        _volume = Engine::Texture3D<Engine::Formats::R8>(size, size, size);
        auto const step = size > 1 ? 2.f / float(size - 1) : 0.f;
        auto const thickness = std::max(_settings.Thickness, 0.01f);

        for (std::size_t z = 0; z < size; ++z) {
            auto const zn = size > 1 ? -1.f + step * z : 0.f;
            for (std::size_t y = 0; y < size; ++y) {
                auto const yn = size > 1 ? -1.f + step * y : 0.f;
                for (std::size_t x = 0; x < size; ++x) {
                    auto const xn = size > 1 ? -1.f + step * x : 0.f;
                    auto const radius = std::sqrt(xn * xn + yn * yn + zn * zn);
                    auto const ripple = _settings.StaticRippleAmp * std::sin(_settings.StaticRippleFreq * radius);
                    auto const radiusWithRipple = radius + ripple;
                    float density = 0.f;
                    for (int layer = 0; layer < shellCount; ++layer) {
                        auto const shellRadius = (float(layer) + 1.f) / (shellCount + 1.f);
                        auto const amplitude = ComputeShellAmplitude(layer, shellCount);
                        auto const delta = (radiusWithRipple - shellRadius) / thickness;
                        density += amplitude * std::exp(-delta * delta);
                    }
                    density = std::clamp(density * _settings.GlobalGain * 0.15f, 0.f, 1.f);
                    _volume.At(x, y, z) = density;
                }
            }
        }
    }

    void SphereVolumeData::UploadVolumeTexture() {
        if (_settings.VolumeSize == 0) {
            return;
        }
        _volumeTexture.UpdateSampler(MakeSamplerOptions());
        _volumeTexture.Update(_volume);
    }

    void SphereVolumeData::UpdateSliceTexture() {
        if (_settings.VolumeSize == 0) {
            return;
        }
        Engine::Texture2D<Engine::Formats::R8> slice(_settings.VolumeSize, _settings.VolumeSize);
        for (std::size_t y = 0; y < _settings.VolumeSize; ++y) {
            for (std::size_t x = 0; x < _settings.VolumeSize; ++x) {
                slice.At(x, y) = _volume.At(x, y, _sliceIndex);
            }
        }
        _sliceTexture.UpdateSampler(MakeSamplerOptions());
        _sliceTexture.Update(slice);
    }
}
