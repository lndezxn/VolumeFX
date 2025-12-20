#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Engine/GL/Program.h"
#include "Engine/GL/RenderItem.h"
#include "Engine/app.h"

namespace VCX::Apps::VolumeFX {
    class App : public Engine::IApp {
    public:
        App();

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
        Engine::GL::UniqueIndexedRenderItem _cube;

        glm::vec2 _orbitAngles { 0.6f, 0.35f };
        float     _cameraDistance = 4.0f;
        glm::vec3 _cameraTarget { 0.0f, 0.0f, 0.0f };
        bool      _autoRotate = true;

        std::array<char, 512> _audioPathBuffer { };
        std::string           _audioStatus = "No audio loaded.";
        float                 _visualizationGain = 1.0f;
        float                 _mockPlaybackTime = 0.0f;

        bool  _isOrbiting = false;
    };
} // namespace VCX::Apps::VolumeFX
