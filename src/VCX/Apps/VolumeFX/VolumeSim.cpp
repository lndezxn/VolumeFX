#include "Apps/VolumeFX/VolumeSim.h"

#include <algorithm>
#include <vector>

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
        _diffuseProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/density_diffuse.comp") })),
        _velAddProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/vel_add_force.comp") })),
        _velAdvectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/vel_advect.comp") })),
        _divergenceProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/divergence.comp") })),
        _pressureJacobiProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/pressure_jacobi.comp") })),
        _velProjectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/vel_subtract_gradient.comp") })) {
        _injectProgram.GetUniforms().SetByName("u_In", 0);
        _advectProgram.GetUniforms().SetByName("u_In", 0);
        _advectProgram.GetUniforms().SetByName("u_Vel", 1);
        _diffuseProgram.GetUniforms().SetByName("u_In", 0);
        _velAddProgram.GetUniforms().SetByName("u_VelIn", 0);
        _velAdvectProgram.GetUniforms().SetByName("u_VelIn", 0);
        _divergenceProgram.GetUniforms().SetByName("u_Vel", 0);
        _pressureJacobiProgram.GetUniforms().SetByName("u_PressureIn", 0);
        _pressureJacobiProgram.GetUniforms().SetByName("u_Div", 1);
        _velProjectProgram.GetUniforms().SetByName("u_VelIn", 0);
        _velProjectProgram.GetUniforms().SetByName("u_Pressure", 1);
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
        _densitySrc = 0;
        _velSrc = 0;
        _pressureSrc = 0;
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
        _densitySrc = 0;
        _velSrc = 0;
        _pressureSrc = 0;
        _initialized = true;
        return true;
    }

    void VolumeSim::step(float dt, float time, float audioGain) {
        if (! _initialized) {
            return;
        }

        auto divUp = [](int v) { return (v + 7) / 8; };
        GLuint groupsX = static_cast<GLuint>(divUp(_size[0]));
        GLuint groupsY = static_cast<GLuint>(divUp(_size[1]));
        GLuint groupsZ = static_cast<GLuint>(divUp(_size[2]));

        auto bindAndDispatch = [&](Engine::GL::UniqueProgram & program, GLuint readTex, GLuint writeTex, int samplerUnit = 0) {
            glActiveTexture(GL_TEXTURE0 + samplerUnit);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
            auto const useProgram = program.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
        };

        auto bindAndDispatchRGBA = [&](Engine::GL::UniqueProgram & program, GLuint readTex, GLuint writeTex, int samplerUnit = 0) {
            glActiveTexture(GL_TEXTURE0 + samplerUnit);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            auto const useProgram = program.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
        };

        // velocity add force (ping-pong)
        {
            auto & uni = _velAddProgram.GetUniforms();
            uni.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            uni.SetByName("u_Time", time);
            uni.SetByName("u_Dt", dt);
            uni.SetByName("u_AudioGain", audioGain);
            uni.SetByName("u_ForceStrength", _forceStrength);
            uni.SetByName("u_Sigma", _forceSigma);

            GLuint readTex = _velocity[_velSrc];
            GLuint writeTex = _velocity[1 - _velSrc];
            bindAndDispatchRGBA(_velAddProgram, readTex, writeTex, 0);
            _velSrc = 1 - _velSrc;
        }

        // velocity advect (ping-pong)
        {
            auto & uni = _velAdvectProgram.GetUniforms();
            uni.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            uni.SetByName("u_Dt", dt);
            uni.SetByName("u_VelDamp", _velDamp);

            GLuint readTex = _velocity[_velSrc];
            GLuint writeTex = _velocity[1 - _velSrc];
            bindAndDispatchRGBA(_velAdvectProgram, readTex, writeTex, 0);
            _velSrc = 1 - _velSrc;
        }

        // divergence
        {
            auto & uni = _divergenceProgram.GetUniforms();
            uni.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, _velocity[_velSrc]);
            glBindImageTexture(0, _divergence, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
            auto const useProgram = _divergenceProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
        }

        // clear pressure to zero before Jacobi iterations
        auto clearTex = [&](GLuint tex, GLenum format, GLenum type, int components) {
            std::vector<float> zeros(static_cast<std::size_t>(_size[0]) * _size[1] * _size[2] * components, 0.0f);
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, _size[0], _size[1], _size[2], format, type, zeros.data());
            glBindTexture(GL_TEXTURE_3D, 0);
        };
        clearTex(_pressure[0], GL_RED, GL_FLOAT, 1);
        clearTex(_pressure[1], GL_RED, GL_FLOAT, 1);
        _pressureSrc = 0;

        // Jacobi iterations for pressure
        {
            auto & uni = _pressureJacobiProgram.GetUniforms();
            uni.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            uni.SetByName("u_Alpha", 1.0f);
            uni.SetByName("u_InvBeta", 1.0f / 6.0f);

            for (int i = 0; i < _jacobiIters; ++i) {
                GLuint readTex = _pressure[_pressureSrc];
                GLuint writeTex = _pressure[1 - _pressureSrc];

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_3D, readTex);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_3D, _divergence);
                glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

                auto const useProgram = _pressureJacobiProgram.Use();
                glDispatchCompute(groupsX, groupsY, groupsZ);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_3D, 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_3D, 0);
                glActiveTexture(GL_TEXTURE0);

                _pressureSrc = 1 - _pressureSrc;
            }
        }

        // subtract pressure gradient from velocity (ping-pong)
        {
            auto & uni = _velProjectProgram.GetUniforms();
            uni.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));

            GLuint readVel = _velocity[_velSrc];
            GLuint writeVel = _velocity[1 - _velSrc];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readVel);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, _pressure[_pressureSrc]);
            glBindImageTexture(0, writeVel, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

            auto const useProgram = _velProjectProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);

            _velSrc = 1 - _velSrc;
        }

        //  density inject
        {
            auto & injectUniforms = _injectProgram.GetUniforms();
            injectUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            injectUniforms.SetByName("u_Time", time);
            injectUniforms.SetByName("u_Dt", dt);
            injectUniforms.SetByName("u_AudioGain", audioGain);
            injectUniforms.SetByName("u_EmitStrength", _emitStrength);
            injectUniforms.SetByName("u_Sigma", _sigma);
            injectUniforms.SetByName("u_Dissipation", _dissipation);

            GLuint readTex = _density[_densitySrc];
            GLuint writeTex = _density[1 - _densitySrc];
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
            auto const useProgram = _injectProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
            _densitySrc = 1 - _densitySrc;
        }

        // density advect using projected velocity
        {
            auto & advectUniforms = _advectProgram.GetUniforms();
            advectUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            advectUniforms.SetByName("u_Dt", dt);
            advectUniforms.SetByName("u_AdvectStrength", _advectStrength);

            GLuint readDensity = _density[_densitySrc];
            GLuint writeDensity = _density[1 - _densitySrc];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readDensity);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, _velocity[_velSrc]);
            glBindImageTexture(0, writeDensity, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

            auto const useProgram = _advectProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);

            _densitySrc = 1 - _densitySrc;
        }

        // Optional density diffusion
        if (_diffuseEnabled) {
            auto & diffuseUniforms = _diffuseProgram.GetUniforms();
            diffuseUniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
            diffuseUniforms.SetByName("u_DiffK", _diffK);

            GLuint readTex = _density[_densitySrc];
            GLuint writeTex = _density[1 - _densitySrc];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, readTex);
            glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

            auto const useProgram = _diffuseProgram.Use();
            glDispatchCompute(groupsX, groupsY, groupsZ);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glBindTexture(GL_TEXTURE_3D, 0);
            glActiveTexture(GL_TEXTURE0);
            _densitySrc = 1 - _densitySrc;
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
        return _initialized ? _density[_densitySrc] : 0;
    }

    void VolumeSim::SetForceStrength(float v) {
        _forceStrength = std::max(0.0f, v);
    }

    float VolumeSim::ForceStrength() const {
        return _forceStrength;
    }

    void VolumeSim::SetForceSigma(float v) {
        _forceSigma = std::clamp(v, 0.01f, 0.6f);
    }

    float VolumeSim::ForceSigma() const {
        return _forceSigma;
    }

    void VolumeSim::SetVelDamp(float v) {
        _velDamp = std::clamp(v, 0.90f, 0.9999f);
    }

    float VolumeSim::VelDamp() const {
        return _velDamp;
    }

    void VolumeSim::SetJacobiIters(int iters) {
        _jacobiIters = std::clamp(iters, 1, 200);
    }

    int VolumeSim::JacobiIters() const {
        return _jacobiIters;
    }

    void VolumeSim::SetAdvectStrength(float v) {
        _advectStrength = std::clamp(v, 0.1f, 8.0f);
    }

    float VolumeSim::AdvectStrength() const {
        return _advectStrength;
    }
} // namespace VCX::Apps::VolumeFX