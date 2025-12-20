#pragma once

#include <array>
#include <string>
#include <vector>

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
        void updateAudioReactivity();
        bool loadAudioFile(const char * path);
        void startAudioPlayback(const std::wstring & path, bool loop);
        void stopAudioPlayback();
        float sampleAudioEnvelope(float t) const;
        glm::vec3 cameraPosition() const;

        Engine::GL::UniqueProgram           _program;
        Engine::GL::UniqueProgram           _decayProgram;
        Engine::GL::UniqueProgram           _injectProgram;
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
        std::vector<float>    _audioEnvelope;
        bool                  _audioLoaded = false;
        bool                  _audioLoopPlayback = true;
        bool                  _audioPlaying = false;
        std::wstring          _audioPathW;
        float                 _audioSampleRate = 0.0f;
        float                 _audioDuration = 0.0f;
        float                 _currentAudioLevel = 0.0f;
        float                 _visualizationGain = 1.0f;
        float                 _baseGain = 1.0f;
        bool                  _autoGainEnabled = true;
        float                 _autoGainDepth = 0.35f;
        float                 _autoGainSpeed = 1.2f;
        float                 _autoGainPhase = 0.0f;
        float                 _densityThreshold = 0.02f;
        float                 _dissipation = 0.9992f;
        float                 _emitStrength = 1.0f;
        float                 _emitSigma = 0.08f;
        float                 _emitterRadius = 0.25f;
        int                   _emitterCount = 1;
        float                 _mockPlaybackTime = 0.0f;

        bool  _isOrbiting = false;

        void initDensityTextures();
        void decayDensityField();
        void injectDensityField();
        GLuint densityReadTexture() const;
        GLuint densityWriteTexture() const;
    };
} // namespace VCX::Apps::VolumeFX
