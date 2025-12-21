#pragma once

#include "Engine/app.h"

namespace VCX::Apps::SphereAudioVisualizer {
    class App : public VCX::Engine::IApp {
    public:
        App();
        void OnFrame() override;

    private:
        float _alpha;
    };

    void EnsureLogger();
    int  RunApp();
}
