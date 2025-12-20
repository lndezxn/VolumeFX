#pragma once

#include <array>

#include "Apps/VolumeFX/AudioInput.h"
#include "Apps/VolumeFX/DensityField.h"
#include "Apps/VolumeFX/OrbitCamera.h"
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
        DensityField   _density;
        OrbitCamera    _camera;
        VolumeRenderer _renderer;

        std::array<char, 512> _audioPathBuffer { };
        float                  _densityThreshold = 0.02f;
    };
} // namespace VCX::Apps::VolumeFX
