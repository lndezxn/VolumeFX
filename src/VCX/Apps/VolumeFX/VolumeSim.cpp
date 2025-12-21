#include "Apps/VolumeFX/VolumeSim.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace VCX::Apps::VolumeFX {
    namespace {
        constexpr GLint kInternalFormat = GL_R16F;
    }

    VolumeSim::VolumeSim() :
        _injectProgram(Engine::GL::UniqueProgram({ Engine::GL::SharedShader("assets/shaders/compute/density_inject.comp") })) {
        _injectProgram.GetUniforms().SetByName("u_In", 0);
    }

    VolumeSim::~VolumeSim() {
        destroy();
    }

    void VolumeSim::destroy() {
        for (auto & tex : _density) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
        _size = { 0, 0, 0 };
        _src = 0;
        _initialized = false;
    }

    void VolumeSim::createTexture(GLuint tex, int sx, int sy, int sz) const {
        glBindTexture(GL_TEXTURE_3D, tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(
            GL_TEXTURE_3D,
            0,
            kInternalFormat,
            sx,
            sy,
            sz,
            0,
            GL_RED,
            GL_FLOAT,
            nullptr);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    bool VolumeSim::init(int sx, int sy, int sz) {
        if (sx <= 0 || sy <= 0 || sz <= 0) {
            return false;
        }

        destroy();

        glGenTextures(static_cast<GLsizei>(_density.size()), _density.data());
        std::vector<float> zeros(static_cast<std::size_t>(sx) * sy * sz, 0.0f);
        for (auto tex : _density) {
            createTexture(tex, sx, sy, sz);
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
                GL_RED,
                GL_FLOAT,
                zeros.data());
        }
        glBindTexture(GL_TEXTURE_3D, 0);

        _size = { sx, sy, sz };
        _src = 0;
        _initialized = true;
        return true;
    }

    void VolumeSim::step(float dt, float time, float audioGain) {
        if (! _initialized) {
            return;
        }

        auto & uniforms = _injectProgram.GetUniforms();
        uniforms.SetByName("u_Size", glm::ivec3(_size[0], _size[1], _size[2]));
        uniforms.SetByName("u_Time", time);
        uniforms.SetByName("u_Dt", dt);
        uniforms.SetByName("u_AudioGain", audioGain);
        uniforms.SetByName("u_EmitStrength", _emitStrength);
        uniforms.SetByName("u_Sigma", _sigma);
        uniforms.SetByName("u_Dissipation", _dissipation);

        GLuint readTex = _density[_src];
        GLuint writeTex = _density[1 - _src];

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, readTex);

        glBindImageTexture(0, writeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

        auto divUp = [](int v) { return (v + 7) / 8; };
        GLuint groupsX = static_cast<GLuint>(divUp(_size[0]));
        GLuint groupsY = static_cast<GLuint>(divUp(_size[1]));
        GLuint groupsZ = static_cast<GLuint>(divUp(_size[2]));
        auto const useProgram = _injectProgram.Use(); // ensure program is bound for dispatch
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE0);

        _src = 1 - _src;
    }

    GLuint VolumeSim::densityTex() const {
        return _initialized ? _density[_src] : 0;
    }
} // namespace VCX::Apps::VolumeFX