#include "Apps/VolumeFX/DensityField.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "Engine/prelude.hpp"

namespace VCX::Apps::VolumeFX {
    DensityField::DensityField() :
        _decayProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/density_decay.comp") })),
        _injectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/density_inject.comp") })) {
        _decayProgram.GetUniforms().SetByName("u_In", 1);
        initTextures();
    }

    DensityField::~DensityField() {
        for (auto & tex : _densityTex) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
    }

    void DensityField::initTextures() {
        for (auto & tex : _densityTex) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }

        std::vector<float> density(static_cast<std::size_t>(_gridSize.x) * _gridSize.y * _gridSize.z, 0.0f);
        glm::vec3 const gridSizeF = glm::vec3(_gridSize);
        glm::vec3 const denom = glm::max(gridSizeF - glm::vec3(1.0f), glm::vec3(1.0f));
        glm::vec3 const center(0.5f);
        float const radius = 0.35f;
        float const falloff = 0.12f;

        std::size_t idx = 0;
        for (int z = 0; z < _gridSize.z; ++z) {
            for (int y = 0; y < _gridSize.y; ++y) {
                for (int x = 0; x < _gridSize.x; ++x) {
                    glm::vec3 const pos = glm::vec3(x, y, z) / denom;
                    float const dist = glm::length(pos - center);
                    float const value = glm::clamp(1.0f - glm::smoothstep(radius - falloff, radius, dist), 0.0f, 1.0f);
                    density[idx++] = value;
                }
            }
        }

        glGenTextures(static_cast<GLsizei>(_densityTex.size()), _densityTex.data());

        auto const configureTexture = [&](GLuint tex) {
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexImage3D(
                GL_TEXTURE_3D,
                0,
                GL_R16F,
                _gridSize.x,
                _gridSize.y,
                _gridSize.z,
                0,
                GL_RED,
                GL_FLOAT,
                nullptr);
            glTexSubImage3D(
                GL_TEXTURE_3D,
                0,
                0,
                0,
                0,
                _gridSize.x,
                _gridSize.y,
                _gridSize.z,
                GL_RED,
                GL_FLOAT,
                density.data());
        };

        for (auto const tex : _densityTex) {
            configureTexture(tex);
        }

        glBindTexture(GL_TEXTURE_3D, 0);
        _densitySrc = 0;
    }

    void DensityField::Inject(float audioLevel, float visualizationGain, float deltaTime, float time, bool autoGainEnabled) {
        if (_densityTex[0] == 0) {
            return;
        }

        auto const useProgram = _injectProgram.Use();
        auto & uniforms = _injectProgram.GetUniforms();
        uniforms.SetByName("u_Size", _gridSize);
        float const clampedLevel = glm::clamp(audioLevel, 0.0f, 1.5f);
        float const levelBoost = autoGainEnabled ? glm::clamp(0.5f + 1.5f * clampedLevel, 0.5f, 2.5f) : 1.0f;
        float const frameScale = glm::clamp(deltaTime * 60.0f, 0.25f, 3.0f);
        uniforms.SetByName("u_EmitStrength", _emitStrength * visualizationGain * levelBoost * frameScale);
        uniforms.SetByName("u_Sigma", _emitSigma);
        uniforms.SetByName("u_EmitterRadius", _emitterRadius);
        uniforms.SetByName("u_Time", time);
        uniforms.SetByName("u_Emitters", std::min(_emitterCount, 4));

        glBindImageTexture(0, ReadTexture(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R16F);

        GLuint const groupsX = static_cast<GLuint>((_gridSize.x + 7) / 8);
        GLuint const groupsY = static_cast<GLuint>((_gridSize.y + 7) / 8);
        GLuint const groupsZ = static_cast<GLuint>((_gridSize.z + 7) / 8);
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void DensityField::Decay() {
        if (_densityTex[0] == 0 || _densityTex[1] == 0) {
            return;
        }

        auto & uniforms = _decayProgram.GetUniforms();
        uniforms.SetByName("u_Size", _gridSize);
        uniforms.SetByName("u_Dissipation", _dissipation);
        auto const useProgram = _decayProgram.Use();

        glBindImageTexture(0, _densityTex[1 - _densitySrc], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, ReadTexture());

        auto const divUp = [](int value) {
            return (value + 7) / 8;
        };
        GLuint const groupsX = static_cast<GLuint>(divUp(_gridSize.x));
        GLuint const groupsY = static_cast<GLuint>(divUp(_gridSize.y));
        GLuint const groupsZ = static_cast<GLuint>(divUp(_gridSize.z));
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE0);
        _densitySrc = 1 - _densitySrc;
    }

    GLuint DensityField::ReadTexture() const {
        return _densityTex[_densitySrc];
    }

    glm::ivec3 DensityField::GridSize() const {
        return _gridSize;
    }

    int DensityField::RaymarchSteps() const {
        return _raymarchSteps;
    }

    void DensityField::SetRaymarchSteps(int steps) {
        _raymarchSteps = std::max(1, steps);
    }

    float DensityField::Dissipation() const {
        return _dissipation;
    }

    void DensityField::SetDissipation(float value) {
        _dissipation = value;
    }

    float DensityField::EmitStrength() const {
        return _emitStrength;
    }

    void DensityField::SetEmitStrength(float value) {
        _emitStrength = value;
    }

    float DensityField::EmitSigma() const {
        return _emitSigma;
    }

    void DensityField::SetEmitSigma(float value) {
        _emitSigma = value;
    }

    float DensityField::EmitterRadius() const {
        return _emitterRadius;
    }

    void DensityField::SetEmitterRadius(float value) {
        _emitterRadius = value;
    }

    int DensityField::EmitterCount() const {
        return _emitterCount;
    }

    void DensityField::SetEmitterCount(int count) {
        _emitterCount = std::clamp(count, 1, 4);
    }

    void DensityField::Reset() {
        initTextures();
    }
} // namespace VCX::Apps::VolumeFX
