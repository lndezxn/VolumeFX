#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr std::size_t kMinVolumeSize = 32;
        constexpr std::size_t kMaxVolumeSize = 256;
        constexpr float        kMinThickness  = 0.01f;
        constexpr float        kMaxBaseThickness = 0.5f;
        constexpr float        kMaxAmpScale   = 5.f;
        constexpr float        kMinGlobalGain = 0.1f;
        constexpr float        kMaxGlobalGain = 5.f;
        constexpr float        kMinSmoothing  = 0.f;
        constexpr float        kMaxSmoothing  = 1.f;
        constexpr float        kMinTilt       = -1.f;
        constexpr float        kMaxTilt       = 1.f;
        constexpr float        kMinRadius     = 0.05f;
        constexpr float        kMaxRadius     = 1.f;

        inline VCX::Engine::GL::SamplerOptions MakeSamplerOptions() {
            return VCX::Engine::GL::SamplerOptions {
                .WrapU     = VCX::Engine::GL::WrapMode::Clamp,
                .WrapV     = VCX::Engine::GL::WrapMode::Clamp,
                .WrapW     = VCX::Engine::GL::WrapMode::Clamp,
                .MinFilter = VCX::Engine::GL::FilterMode::Linear,
                .MagFilter = VCX::Engine::GL::FilterMode::Linear,
            };
        }
    }

    SphereVolumeData::SphereVolumeData() {
        Regenerate();
    }

    SphereVolumeData::Settings const & SphereVolumeData::GetSettings() const {
        return _settings;
    }

    void SphereVolumeData::SetSettings(Settings settings) {
        settings.VolumeSize     = std::clamp(settings.VolumeSize, kMinVolumeSize, kMaxVolumeSize);
        settings.AmpScale       = std::clamp(settings.AmpScale, 0.f, kMaxAmpScale);
        settings.ThicknessScale = std::clamp(settings.ThicknessScale, 0.f, 5.f);
        settings.BaseThickness  = std::clamp(settings.BaseThickness, kMinThickness, kMaxBaseThickness);
        settings.GlobalGain     = std::clamp(settings.GlobalGain, kMinGlobalGain, kMaxGlobalGain);
        settings.SmoothingFactor = std::clamp(settings.SmoothingFactor, kMinSmoothing, kMaxSmoothing);
        settings.Tilt           = std::clamp(settings.Tilt, kMinTilt, kMaxTilt);
        _settings = settings;
        EnsureBandTables(_bandCount);
    }

    void SphereVolumeData::Regenerate() {
        if (_settings.VolumeSize == 0) {
            _settings.VolumeSize = kMinVolumeSize;
        }
        _volume = Engine::Texture3D<Engine::Formats::R8>(_settings.VolumeSize, _settings.VolumeSize, _settings.VolumeSize);
        _sliceIndex = std::clamp(_sliceIndex, std::size_t{0}, _settings.VolumeSize == 0 ? 0 : _settings.VolumeSize - 1);
        EnsureBandTables(_bandCount);
        UpdateSliceTexture();
    }

    SphereVolumeData::BuildStats SphereVolumeData::UpdateVolume(std::vector<float> const & energies) {
        BuildStats stats;
        if (_settings.VolumeSize == 0) {
            return stats;
        }

        std::size_t desiredBands = std::max<std::size_t>(1, energies.size());
        if (desiredBands != _bandCount) {
            EnsureBandTables(desiredBands);
        }
        if (_smoothedEnergies.size() != _bandCount) {
            _smoothedEnergies.assign(_bandCount, 0.f);
        }

        float smoothing = std::clamp(_settings.SmoothingFactor, kMinSmoothing, kMaxSmoothing);
        for (std::size_t i = 0; i < _bandCount; ++i) {
            float current = (i < energies.size()) ? energies[i] : 0.f;
            if (smoothing >= 1.f) {
                _smoothedEnergies[i] = current;
            } else {
                _smoothedEnergies[i] += smoothing * (current - _smoothedEnergies[i]);
            }
        }

        auto const buildStart = std::chrono::high_resolution_clock::now();
        BuildVolume(_smoothedEnergies);
        auto const buildEnd = std::chrono::high_resolution_clock::now();
        stats.BuildMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();

        auto const uploadStart = std::chrono::high_resolution_clock::now();
        UploadVolumeTexture();
        auto const uploadEnd = std::chrono::high_resolution_clock::now();
        stats.UploadMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
        return stats;
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

    void SphereVolumeData::BuildVolume(std::vector<float> const & energies) {
        if (_settings.VolumeSize == 0) {
            return;
        }
        auto const size = _settings.VolumeSize;
        auto const step = size > 1 ? 2.f / float(size - 1) : 0.f;
        auto const baseThickness = std::max(_settings.BaseThickness, kMinThickness);
        auto const globalGain = std::clamp(_settings.GlobalGain, kMinGlobalGain, kMaxGlobalGain);

        for (std::size_t z = 0; z < size; ++z) {
            auto const zn = size > 1 ? -1.f + step * z : 0.f;
            for (std::size_t y = 0; y < size; ++y) {
                auto const yn = size > 1 ? -1.f + step * y : 0.f;
                for (std::size_t x = 0; x < size; ++x) {
                    auto const xn = size > 1 ? -1.f + step * x : 0.f;
                    auto const radius = std::sqrt(xn * xn + yn * yn + zn * zn);
                    float density = 0.f;
                    for (std::size_t band = 0; band < _bandCount; ++band) {
                        float energy = band < energies.size() ? energies[band] : 0.f;
                        float radiusTarget = _bandBaseRadius[band] * (1.f + _settings.AmpScale * energy);
                        float thickness = baseThickness * (1.f + _settings.ThicknessScale * energy);
                        thickness = std::max(thickness, kMinThickness);
                        float delta = (radius - radiusTarget) / thickness;
                        density += _bandGains[band] * energy * std::exp(-delta * delta);
                    }
                    float value = std::clamp(density * globalGain, 0.f, 1.f);
                    _volume.At(x, y, z) = value;
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

    void SphereVolumeData::EnsureBandTables(std::size_t bandCount) {
        if (bandCount == 0) {
            bandCount = 1;
        }
        _bandCount = bandCount;
        _bandBaseRadius.resize(_bandCount);
        _bandGains.resize(_bandCount);
        _smoothedEnergies.resize(_bandCount);
        std::fill(_smoothedEnergies.begin(), _smoothedEnergies.end(), 0.f);

        float logMin = std::log(kMinRadius);
        float logMax = std::log(kMaxRadius);
        for (std::size_t i = 0; i < _bandCount; ++i) {
            float t = static_cast<float>(i + 1) / static_cast<float>(_bandCount + 1);
            float radius = 0.f;
            if (_settings.RadiusLayout == RadiusDistribution::Log) {
                radius = std::exp(logMin + (logMax - logMin) * t);
            } else {
                radius = kMinRadius + (kMaxRadius - kMinRadius) * t;
            }
            _bandBaseRadius[i] = std::clamp(radius, kMinRadius, kMaxRadius);
            _bandGains[i] = ComputeBandGain(i, _bandCount);
        }
    }

    float SphereVolumeData::ComputeBandGain(std::size_t bandIndex, std::size_t bandCount) const {
        if (bandCount <= 1) {
            return 1.f;
        }
        float position = static_cast<float>(bandIndex) / static_cast<float>(bandCount - 1);
        float tilt = _settings.Tilt;
        float gain = 1.f + tilt * (position * 2.f - 1.f);
        return std::clamp(gain, 0.1f, 3.f);
    }
}
