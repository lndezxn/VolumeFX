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

        GLuint densityTex() const;

    private:
        void destroy();
        void createTexture(GLuint tex, int sx, int sy, int sz) const;

        Engine::GL::UniqueProgram _injectProgram;

        std::array<GLuint, 2> _density { 0, 0 };
        std::array<int, 3>    _size { 0, 0, 0 };
        int                   _src = 0;
        bool                  _initialized = false;

        float                 _emitStrength = 3.0f;
        float                 _sigma = 0.12f;
        float                 _dissipation = 0.995f;
    };
} // namespace VCX::Apps::VolumeFX