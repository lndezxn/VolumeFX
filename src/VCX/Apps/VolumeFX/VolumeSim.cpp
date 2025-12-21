#include "Apps/VolumeFX/VolumeSim.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace VCX::Apps::VolumeFX {
    namespace {
        constexpr GLint kDensityFormat = GL_R16F;
        constexpr GLint kPressureFormat = GL_R16F;
        constexpr GLint kDivergenceFormat = GL_R16F;
        constexpr GLint kVelocityFormat = GL_RGBA16F;
    }

    VolumeSim::VolumeSim() :
        _injectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/density_inject.comp") })),
        _advectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/density_advect.comp") })),
        _diffuseProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/density_diffuse.comp") })) {
        _injectProgram.GetUniforms().SetByName("u_In", 0);
        _advectProgram.GetUniforms().SetByName("u_In", 0);
        _diffuseProgram.GetUniforms().SetByName("u_In", 0);
    }

    VolumeSim::~VolumeSim() {
        destroy();
    }

    void VolumeSim::destroy() {
        auto deleteArray = [](auto & arr) {
            for (auto & tex : arr) {
                if (tex != 0) {
                    glDeleteTextures(1, &tex);
                    tex = 0;
                }
            }
        };
        deleteArray(_density);
        deleteArray(_velocity);
        deleteArray(_pressure);
        if (_divergence != 0) {
            glDeleteTextures(1, &_divergence);
            _divergence = 0;
        }
        _size = { 0, 0, 0 };
        _src = 0;
        _initialized = false;
    }

    void VolumeSim::createTexture(GLuint tex, int sx, int sy, int sz, GLint internalFormat, GLenum format, GLenum type) const {
        glBindTexture(GL_TEXTURE_3D, tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(
            GL_TEXTURE_3D,
            0,
            internalFormat,
            sx,
            sy,
            sz,
            0,
            format,
            type,
            nullptr);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    bool VolumeSim::init(int sx, int sy, int sz) {
        if (sx <= 0 || sy <= 0 || sz <= 0) {
            return false;
        }

        destroy();

        auto clearTex = [&](GLuint tex, GLenum format, GLenum type, int components) {
            std::vector<float> zeros(static_cast<std::size_t>(sx) * sy * sz * components, 0.0f);
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexSubImage3D(
                GL_TEXTURE_3D,
                0,
                0,
                0,
                0,
                sx,
                sy,
                sz,
                format,
                type,
                zeros.data());
        };

        glGenTextures(static_cast<GLsizei>(_density.size()), _density.data());
        glGenTextures(static_cast<GLsizei>(_velocity.size()), _velocity.data());
        glGenTextures(static_cast<GLsizei>(_pressure.size()), _pressure.data());
        glGenTextures(1, &_divergence);

        for (auto tex : _density) {
            createTexture(tex, sx, sy, sz, kDensityFormat, GL_RED, GL_FLOAT);
            clearTex(tex, GL_RED, GL_FLOAT, 1);
        }
        for (auto tex : _velocity) {
            createTexture(tex, sx, sy, sz, kVelocityFormat, GL_RGBA, GL_FLOAT);
            clearTex(tex, GL_RGBA, GL_FLOAT, 4);
        }
        for (auto tex : _pressure) {
            createTexture(tex, sx, sy, sz, kPressureFormat, GL_RED, GL_FLOAT);
            clearTex(tex, GL_RED, GL_FLOAT, 1);
        }
        createTexture(_divergence, sx, sy, sz, kDivergenceFormat, GL_RED, GL_FLOAT);
        clearTex(_divergence, GL_RED, GL_FLOAT, 1);

        _size = { sx, sy, sz };
        _src = 0;
        _initialized = true;
        return true;
    }

    void VolumeSim::step(float dt, float time, float audioGain) {
        if (! _initialized) {
            return;
        }

        // 1) Inject / dissipate
        auto & injectUniforms = _injectProgram.GetUniforms();
        injectUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
        injectUniforms.SetByName("u_Time", time);
        injectUniforms.SetByName("u_Dt", dt);
        injectUniforms.SetByName("u_AudioGain", audioGain);
        injectUniforms.SetByName("u_EmitStrength", _emitStrength);
        injectUniforms.SetByName("u_Sigma", _sigma);
        injectUniforms.SetByName("u_Dissipation", _dissipation);

        auto divUp = [](int v) { return (v + 7) / 8; };
        GLuint groupsX = static_cast<GLuint>(divUp(_size[0]));
        GLuint groupsY = static_cast<GLuint>(divUp(_size[1]));
        GLuint groupsZ = static_cast<GLuint>(divUp(_size[2]));

        {
            GLuint readTex = _density[_src];
            GLuint writeTex = _density[1 - _src];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

            auto const useProgram = _injectProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
            _src = 1 - _src;
        }

        // 2) Advect to create trailing swirl
        auto & advectUniforms = _advectProgram.GetUniforms();
        advectUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
        advectUniforms.SetByName("u_Time", time);
        advectUniforms.SetByName("u_Dt", dt);
        advectUniforms.SetByName("u_AdvectStrength", _advectStrength);
        advectUniforms.SetByName("u_Swirl", _swirl);
        advectUniforms.SetByName("u_Up", _up);
        advectUniforms.SetByName("u_NoiseStrength", _noiseStrength);
        advectUniforms.SetByName("u_NoiseFreq", _noiseFreq);

        {
            GLuint readTex = _density[_src];
            GLuint writeTex = _density[1 - _src];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

            auto const useProgram = _advectProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
            _src = 1 - _src;
        }

        // 3) Optional diffuse to feather edges
        if (_diffuseEnabled) {
            auto & diffuseUniforms = _diffuseProgram.GetUniforms();
            diffuseUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            diffuseUniforms.SetByName("u_DiffK", _diffK);

            GLuint readTex = _density[_src];
            GLuint writeTex = _density[1 - _src];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

            auto const useProgram = _diffuseProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
            _src = 1 - _src;
        }
    }

    void VolumeSim::SetDiffuseEnabled(bool enabled) {
        _diffuseEnabled = enabled;
    }

    bool VolumeSim::DiffuseEnabled() const {
        return _diffuseEnabled;
    }

    void VolumeSim::SetDiffusionK(float k) {
        _diffK = std::clamp(k, 0.0f, 0.5f);
    }

    float VolumeSim::DiffusionK() const {
        return _diffK;
    }

    GLuint VolumeSim::densityTex() const {
        return _initialized ? _density[_src] : 0;
    }
} // namespace VCX::Apps::VolumeFX