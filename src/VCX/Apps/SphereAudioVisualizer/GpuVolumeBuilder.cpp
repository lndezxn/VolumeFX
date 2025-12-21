#include "Apps/SphereAudioVisualizer/GpuVolumeBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include <glad/glad.h>

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr std::size_t kGroupSize = 8;

        void BindVolumeImage(GLuint texture) {
            if (texture == 0) {
                return;
            }
            glBindImageTexture(
                0,
                texture,
                0,
                GL_TRUE,
                0,
                GL_WRITE_ONLY,
                GL_R16F);
        }
    }

    GpuVolumeBuilder::GpuVolumeBuilder():
        _computeProgram({ VCX::Engine::GL::SharedShader("assets/shaders/spherevis_build_volume.comp") }) {
        glGenQueries(1, &_timeQuery);
    }

    GpuVolumeBuilder::~GpuVolumeBuilder() {
        if (_timeQuery) {
            glDeleteQueries(1, &_timeQuery);
            _timeQuery = 0;
        }
    }

    void GpuVolumeBuilder::EnsureResources(std::size_t volumeSize) {
        volumeSize = std::clamp(volumeSize, kMinVolumeSize, kMaxVolumeSize);
        if (volumeSize == 0) {
            return;
        }
        if (_volumeSize == volumeSize) {
            return;
        }
        _volumeSize = volumeSize;
        EnsureTextureAllocated(_volumeSize);
    }

    void GpuVolumeBuilder::EnsureTextureAllocated(std::size_t size) {
        auto const useTex = _volumeTexture.Use();
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, static_cast<GLsizei>(size), static_cast<GLsizei>(size), static_cast<GLsizei>(size), 0, GL_RED, GL_HALF_FLOAT, nullptr);
    }

    float GpuVolumeBuilder::ComputeBandGain(std::size_t bandIndex, std::size_t bandCount, float tilt) const {
        if (bandCount <= 1) {
            return 1.f;
        }
        float position = static_cast<float>(bandIndex) / static_cast<float>(bandCount - 1);
        float gain = 1.f + tilt * (position * 2.f - 1.f);
        return std::clamp(gain, 0.1f, 3.f);
    }

    void GpuVolumeBuilder::UpdateBandTables(std::size_t bandCount, SphereVolumeData::Settings const & settings) {
        if (bandCount == 0) {
            bandCount = 1;
        }
        bandCount = std::min(bandCount, kMaxBands);
        if (_bandCount == bandCount && _radiusLayout == settings.RadiusLayout && !_bandBaseRadius.empty()) {
            return;
        }
        _bandCount = bandCount;
        _radiusLayout = settings.RadiusLayout;
        _bandBaseRadius.assign(_bandCount, 0.f);
        _bandGains.assign(_bandCount, 0.f);
        _smoothedEnergies.assign(_bandCount, 0.f);

        float logMin = std::log(kMinRadius);
        float logMax = std::log(kMaxRadius);
        for (std::size_t i = 0; i < _bandCount; ++i) {
            float t = static_cast<float>(i + 1) / static_cast<float>(_bandCount + 1);
            float radius = 0.f;
            if (_radiusLayout == SphereVolumeData::RadiusDistribution::Log) {
                radius = std::exp(logMin + (logMax - logMin) * t);
            } else {
                radius = kMinRadius + (kMaxRadius - kMinRadius) * t;
            }
            _bandBaseRadius[i] = std::clamp(radius, kMinRadius, kMaxRadius);
            _bandGains[i] = ComputeBandGain(i, _bandCount, settings.Tilt);
        }
    }

    void GpuVolumeBuilder::UpdateSmoothedEnergies(std::vector<float> const & energies, float smoothingFactor) {
        if (_smoothedEnergies.size() != _bandCount) {
            _smoothedEnergies.assign(_bandCount, 0.f);
        }
        float smoothing = std::clamp(smoothingFactor, kMinSmoothing, kMaxSmoothing);
        for (std::size_t i = 0; i < _bandCount; ++i) {
            float current = (i < energies.size()) ? energies[i] : 0.f;
            if (smoothing >= 1.f) {
                _smoothedEnergies[i] = current;
            } else {
                _smoothedEnergies[i] += smoothing * (current - _smoothedEnergies[i]);
            }
        }
    }

    GpuVolumeBuilder::BuildStats GpuVolumeBuilder::DispatchBuild(std::vector<float> const & energies, SphereVolumeData::Settings const & settings) {
        BuildStats stats;
        if (_volumeSize == 0) {
            return stats;
        }
        std::size_t desiredBands = std::max<std::size_t>(1, energies.size());
        UpdateBandTables(desiredBands, settings);
        UpdateSmoothedEnergies(energies, settings.SmoothingFactor);

        float const ampScale = std::clamp(settings.AmpScale, 0.f, kMaxAmpScale);
        float const thicknessScale = std::clamp(settings.ThicknessScale, 0.f, 5.f);
        float const baseThickness = std::max(settings.BaseThickness, kMinThickness);
        float const globalGain = std::clamp(settings.GlobalGain, kMinGlobalGain, kMaxGlobalGain);

        glUseProgram(_computeProgram.Get());
        auto const setUniform = [this](char const * name, auto value) {
            auto const location = glGetUniformLocation(_computeProgram.Get(), name);
            if (location >= 0) {
                if constexpr (std::is_same_v<decltype(value), float>) {
                    glUniform1f(location, value);
                } else if constexpr (std::is_same_v<decltype(value), int>) {
                    glUniform1i(location, value);
                }
            }
        };
        setUniform("uVolumeSize", static_cast<int>(_volumeSize));
        setUniform("uNumBands", static_cast<int>(_bandCount));
        setUniform("uBaseThickness", baseThickness);
        setUniform("uGlobalGain", globalGain);
        setUniform("uAmpScale", ampScale);
        setUniform("uThicknessScale", thicknessScale);

        auto const uploadArray = [this](char const * name, std::vector<float> const & data) {
            auto const location = glGetUniformLocation(_computeProgram.Get(), name);
            if (location >= 0) {
                glUniform1fv(location, static_cast<GLsizei>(data.size()), data.data());
            }
        };
        uploadArray("uBandBaseRadius", _bandBaseRadius);
        uploadArray("uBandGains", _bandGains);
        uploadArray("uEnergies", _smoothedEnergies);

        BindVolumeImage(_volumeTexture.Get());

        glBeginQuery(GL_TIME_ELAPSED, _timeQuery);
        auto const groups = static_cast<GLuint>((_volumeSize + kGroupSize - 1) / kGroupSize);
        glDispatchCompute(groups, groups, groups);
        glEndQuery(GL_TIME_ELAPSED);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(_timeQuery, GL_QUERY_RESULT, &elapsed);
        stats.BuildMs = static_cast<float>(elapsed) * 1e-6f;
        stats.UploadMs = 0.f;
        _lastBuildMs = stats.BuildMs;
        return stats;
    }

    GLuint GpuVolumeBuilder::GetVolumeTexture() const {
        return _volumeTexture.Get();
    }

    float GpuVolumeBuilder::GetLastBuildMs() const {
        return _lastBuildMs;
    }
} // namespace VCX::Apps::SphereAudioVisualizer
