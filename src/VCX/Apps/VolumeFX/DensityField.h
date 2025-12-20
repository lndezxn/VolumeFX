#pragma once

#include <array>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Engine/GL/Program.h"

namespace VCX::Apps::VolumeFX {
    class DensityField {
    public:
        DensityField();
        ~DensityField();

        void Reset();
        void Inject(float audioLevel, float visualizationGain, float deltaTime, float time, bool autoGainEnabled);
        void Decay();

        GLuint ReadTexture() const;
        glm::ivec3 GridSize() const;

        int RaymarchSteps() const;
        void SetRaymarchSteps(int steps);

        float Dissipation() const;
        void SetDissipation(float value);

        float EmitStrength() const;
        void SetEmitStrength(float value);

        float EmitSigma() const;
        void SetEmitSigma(float value);

        float EmitterRadius() const;
        void SetEmitterRadius(float value);

        int EmitterCount() const;
        void SetEmitterCount(int count);

    private:
        void initTextures();

        Engine::GL::UniqueProgram _decayProgram;
        Engine::GL::UniqueProgram _injectProgram;
        std::array<GLuint, 2> _densityTex { 0, 0 };
        int _densitySrc = 0;
        glm::ivec3 _gridSize { 64, 64, 64 };
        int _raymarchSteps = 96;

        float _dissipation = 0.9992f;
        float _emitStrength = 1.0f;
        float _emitSigma = 0.08f;
        float _emitterRadius = 0.25f;
        int   _emitterCount = 1;
    };
} // namespace VCX::Apps::VolumeFX
