#pragma once

#include <array>
#include <string>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Engine/GL/Program.h"
#include "Engine/GL/RenderItem.h"
#include "Engine/app.h"

namespace VCX::Apps::VolumeFX {
    class App : public Engine::IApp {
    public:
        App();
        ~App();

        void OnFrame() override;

        struct Vertex {
            glm::vec3 Position;
            glm::vec3 Color;
        };

    private:
        void updateCamera();
        void renderScene();
        void renderUI();
        glm::vec3 cameraPosition() const;

        Engine::GL::UniqueProgram           _program;
        Engine::GL::UniqueProgram           _decayProgram;
        Engine::GL::UniqueIndexedRenderItem _cube;
        std::array<GLuint, 2>               _densityTex { 0, 0 };
        int                                 _densitySrc = 0;
        glm::ivec3                          _gridSize { 64, 64, 64 };
        int                                 _raymarchSteps = 96;

        glm::vec2 _orbitAngles { 0.6f, 0.35f };
        float     _cameraDistance = 4.0f;
        glm::vec3 _cameraTarget { 0.0f, 0.0f, 0.0f };
        bool      _autoRotate = true;

        std::array<char, 512> _audioPathBuffer { };
        std::string           _audioStatus = "No audio loaded.";
        float                 _visualizationGain = 1.0f;
        float                 _densityThreshold = 0.02f;
        float                 _dissipation = 0.995f;
        float                 _mockPlaybackTime = 0.0f;

        bool  _isOrbiting = false;

        void initDensityTextures();
        void decayDensityField();
        GLuint densityReadTexture() const;
        GLuint densityWriteTexture() const;
    };
} // namespace VCX::Apps::VolumeFX
