#pragma once

#include "Apps/SphereAudioVisualizer/SphereVolumeData.hpp"
#include "Engine/app.h"

namespace VCX::Apps::SphereAudioVisualizer {
    class App : public VCX::Engine::IApp {
    public:
        App();
        void OnFrame() override;

    private:
        float _alpha;
        SphereVolumeData _volumeData;
    };

    void EnsureLogger();
    int  RunApp();
}
