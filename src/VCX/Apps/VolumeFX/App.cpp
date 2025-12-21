#include "Apps/VolumeFX/App.h"

#include <cfloat>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "Engine/prelude.hpp"

namespace VCX::Apps::VolumeFX {
    App::App() {
        glEnable(GL_DEPTH_TEST);
        _sim.init(64, 64, 64);
    }

    App::~App() = default;

    void App::OnFrame() {
        float const deltaTime = Engine::GetDeltaTime();

        _camera.Update(ImGui::GetIO(), deltaTime);
        _audio.Update(deltaTime);

        _sim.step(deltaTime, _audio.PlaybackTime(), _audio.VisualizationGain());

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        _sim.SetDiffuseEnabled(_diffuseEnabled);
        _sim.SetDiffusionK(_diffuseK);
        _renderer.Render(_sim.densityTex(), _camera, _audio.VisualizationGain(), _densityThreshold, _showBoundingBox);
        renderUI();
    }

    void App::renderUI() {
        drawAudioPanel();
        drawScenePanel();
    }

    void App::drawAudioPanel() {
        ImGui::SetNextWindowSizeConstraints({ 360, 0 }, { 600, FLT_MAX });
        ImGui::Begin("Audio Input");
        ImGui::TextWrapped("Stream any audio source to drive volume rendering parameters later.");
        ImGui::InputText("Source file", _audioPathBuffer.data(), _audioPathBuffer.size());
        if (ImGui::Button("Load audio")) {
            if (_audioPathBuffer[0] != '\0') {
                _audio.LoadFromPath(_audioPathBuffer.data());
            } else {
                _audio.SetStatus("Please provide a file path.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            _audioPathBuffer.fill('\0');
            _audio.Clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-seed volume")) {
            _sim.init(64, 64, 64);
        }

        bool loopPlayback = _audio.LoopEnabled();
        if (ImGui::Checkbox("Loop playback", &loopPlayback)) {
            _audio.SetLoop(loopPlayback);
            _audio.RestartPlayback();
        }
        ImGui::ProgressBar(_audio.PlaybackRatio(), ImVec2(-1.0f, 0.0f));
        ImGui::Text("Playback: %.2f / %.2f s", _audio.PlaybackTime(), _audio.Duration());
        ImGui::Text("Audio level: %.2f", _audio.CurrentLevel());

        ImGui::Separator();
        float baseGain = _audio.BaseGain();
        if (ImGui::SliderFloat("Base gain", &baseGain, 0.1f, 4.0f, "%.2f")) {
            _audio.SetBaseGain(baseGain);
        }
        bool react = _audio.AutoGainEnabled();
        if (ImGui::Checkbox("React to audio", &react)) {
            _audio.SetAutoGainEnabled(react);
        }
        ImGui::BeginDisabled(! _audio.AutoGainEnabled());
        float depth = _audio.AutoGainDepth();
        if (ImGui::SliderFloat("Mod depth", &depth, 0.0f, 2.0f, "%.2f")) {
            _audio.SetAutoGainDepth(depth);
        }
        float speed = _audio.AutoGainSpeed();
        if (ImGui::SliderFloat("Fallback speed", &speed, 0.1f, 5.0f, "%.2f")) {
            _audio.SetAutoGainSpeed(speed);
        }
        ImGui::EndDisabled();
        ImGui::Text("Live gain: %.2f", _audio.VisualizationGain());
        ImGui::TextWrapped("Status: %s", _audio.Status().c_str());
        ImGui::End();
    }

    void App::drawScenePanel() {
        ImGui::SetNextWindowSizeConstraints({ 260, 0 }, { 480, FLT_MAX });
        ImGui::Begin("Scene Controls");
        bool autoRotate = _camera.AutoRotate();
        if (ImGui::Checkbox("Auto rotate", &autoRotate)) {
            _camera.SetAutoRotate(autoRotate);
        }
        float cameraDistance = _camera.Distance();
        if (ImGui::SliderFloat("Camera distance", &cameraDistance, 1.2f, 14.0f)) {
            _camera.SetDistance(cameraDistance);
        }
        ImGui::TextUnformatted("Right-drag to orbit, scroll to zoom.");
        ImGui::Text("Gain feeds renderer scale: %.2f", _audio.VisualizationGain());
        ImGui::SliderFloat("Density thresh", &_densityThreshold, 0.0f, 0.2f, "%.3f");
        ImGui::Checkbox("Show bounding box", &_showBoundingBox);
        ImGui::Separator();
        ImGui::Checkbox("Diffuse (feather edges)", &_diffuseEnabled);
        ImGui::BeginDisabled(! _diffuseEnabled);
        if (ImGui::SliderFloat("Diffuse k", &_diffuseK, 0.0f, 0.15f, "%.3f")) {
            _diffuseK = std::clamp(_diffuseK, 0.0f, 0.15f);
        }
        ImGui::EndDisabled();
        ImGui::End();
    }
} // namespace VCX::Apps::VolumeFX
