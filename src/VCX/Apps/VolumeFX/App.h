#pragma once

#include <array>

#include "Apps/VolumeFX/AudioInput.h"
#include "Apps/VolumeFX/OrbitCamera.h"
#include "Apps/VolumeFX/VolumeSim.h"
#include "Apps/VolumeFX/VolumeRenderer.h"
#include "Engine/app.h"

namespace VCX::Apps::VolumeFX {
    class App : public Engine::IApp {
    public:
        App();
        ~App();

        void OnFrame() override;

    private:
        void renderUI();
        void drawAudioPanel();
        void drawScenePanel();

        AudioInput     _audio;
        OrbitCamera    _camera;
        VolumeSim      _sim;
        VolumeRenderer _renderer;

        std::array<char, 512> _audioPathBuffer { };
        float                  _densityThreshold = 0.02f;
        bool                   _showBoundingBox = true;

        bool                   _diffuseEnabled = true;
        float                  _diffuseK = 0.05f;

        float                  _forceStrength = 8.0f;
        float                  _forceSigma = 0.10f;
        float                  _velDamp = 0.995f;
        int                    _jacobiIters = 45;
        float                  _advectStrength = 3.0f;
    };
} // namespace VCX::Apps::VolumeFX
