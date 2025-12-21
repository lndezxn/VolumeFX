#pragma once

#include <array>

#include <glad/glad.h>

#include "Engine/GL/Program.h"

namespace VCX::Apps::VolumeFX {
    class VolumeSim {
    public:
        VolumeSim();
        ~VolumeSim();

        bool init(int sx, int sy, int sz);
        void step(float dt, float time, float audioGain);

        void SetDiffuseEnabled(bool enabled);
        bool DiffuseEnabled() const;
        void SetDiffusionK(float k);
        float DiffusionK() const;

        void SetForceStrength(float v);
        float ForceStrength() const;
        void SetForceSigma(float v);
        float ForceSigma() const;
        void SetVelDamp(float v);
        float VelDamp() const;
        void SetJacobiIters(int iters);
        int JacobiIters() const;
        void SetAdvectStrength(float v);
        float AdvectStrength() const;

        GLuint densityTex() const;

    private:
        void destroy();
        void createTexture(GLuint tex, int sx, int sy, int sz, GLint internalFormat, GLenum format, GLenum type) const;

        Engine::GL::UniqueProgram _injectProgram;
        Engine::GL::UniqueProgram _advectProgram;
        Engine::GL::UniqueProgram _diffuseProgram;
        Engine::GL::UniqueProgram _velAddProgram;
        Engine::GL::UniqueProgram _velAdvectProgram;
        Engine::GL::UniqueProgram _divergenceProgram;
        Engine::GL::UniqueProgram _pressureJacobiProgram;
        Engine::GL::UniqueProgram _velProjectProgram;

        std::array<GLuint, 2> _density { 0, 0 };
        std::array<GLuint, 2> _velocity { 0, 0 };
        std::array<GLuint, 2> _pressure { 0, 0 };
        GLuint                 _divergence = 0;
        std::array<int, 3>    _size { 0, 0, 0 };
        int                   _densitySrc = 0;
        int                   _velSrc = 0;
        int                   _pressureSrc = 0;
        bool                  _initialized = false;

        float                 _emitStrength = 3.0f;
        float                 _sigma = 0.12f;
        float                 _dissipation = 0.995f;

        float                 _advectStrength = 3.0f;
        bool                  _diffuseEnabled = true;
        float                 _diffK = 0.05f;

        // velocity forces and stability
        float                 _forceStrength = 8.0f;
        float                 _forceSigma = 0.10f;
        float                 _velDamp = 0.995f;
        int                   _jacobiIters = 45;
    };
} // namespace VCX::Apps::VolumeFX