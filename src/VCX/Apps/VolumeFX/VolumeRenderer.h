#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Engine/GL/Program.h"
#include "Engine/GL/RenderItem.h"

namespace VCX::Apps::VolumeFX {
    class DensityField;
    class OrbitCamera;

    class VolumeRenderer {
    public:
        VolumeRenderer();

        void Render(GLuint densityTex, const OrbitCamera & camera, float visualizationGain, float densityThreshold, bool showBoundingBox);

        struct Vertex {
            glm::vec3 Position;
            glm::vec3 Color;
        };

        int RaymarchSteps() const;
        void SetRaymarchSteps(int steps);

    private:
        Engine::GL::UniqueProgram           _program;
        Engine::GL::UniqueProgram           _boxProgram;
        Engine::GL::UniqueIndexedRenderItem _cube;
        int                                 _raymarchSteps = 96;
    };
} // namespace VCX::Apps::VolumeFX
