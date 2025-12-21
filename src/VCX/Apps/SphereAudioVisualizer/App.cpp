#include "Apps/SphereAudioVisualizer/App.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Assets/bundled.h"
#include "Engine/app.h"
#include "Engine/math.hpp"

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr auto kFullScreenTriangle = std::array<float, 6> {
            -1.f, -1.f,
             3.f, -1.f,
            -1.f,  3.f,
        };

        constexpr glm::vec3 kVolumeMin { -1.f };
        constexpr glm::vec3 kVolumeMax {  1.f };

        struct StatsData {
            uint32_t Steps     = 0;
            uint32_t Rays      = 0;
            uint32_t EarlyExit = 0;
        };

        std::filesystem::path ResolveLogPath() {
            auto path = std::filesystem::path("logs") / "spherevis.log";
            if (auto parent = path.parent_path(); ! parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    return std::filesystem::path("spherevis.log");
                }
            }
            return path;
        }

        float CurrentFps() {
            auto fps = VCX::Engine::GetFramesPerSecond();
            if (fps <= 0.f) {
                auto const dt = VCX::Engine::GetDeltaTime();
                if (dt > 0.f) {
                    fps = 1.f / dt;
                }
            }
            return fps;
        }

        void SetupLogger() {
            static std::once_flag flag;
            std::call_once(flag, [] {
                auto const logPath = ResolveLogPath();
                std::vector<spdlog::sink_ptr> sinks;
                sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
                sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true));

                auto logger = std::make_shared<spdlog::logger>("spherevis", sinks.begin(), sinks.end());
                logger->set_level(spdlog::level::debug);
                logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
                spdlog::set_default_logger(logger);
                spdlog::flush_on(spdlog::level::info);
                spdlog::info("SphereAudioVisualizer logging to {}", logPath.string());
            });
        }
    }

    void EnsureLogger() {
        SetupLogger();
    }

    App::App():
        _alpha(0.5f),
        _volumeProgram({
            VCX::Engine::GL::SharedShader("assets/shaders/spherevis_volume.vert"),
            VCX::Engine::GL::SharedShader("assets/shaders/spherevis_volume.frag"),
        }) {
        SetupLogger();
        spdlog::debug("SphereAudioVisualizer initialized.");

        {
            auto const vaoUse = _fullscreenVAO.Use();
            auto const vboUse = _fullscreenVBO.Use();
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kFullScreenTriangle.size() * sizeof(float)), kFullScreenTriangle.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        }

        glGenBuffers(1, &_statsBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _statsBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(StatsData), nullptr, GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        ResetStatsBuffer();

        _volumeProgram.GetUniforms().SetByName("uVolumeTexture", 0);
        _volumeData.Regenerate();
    }

    App::~App() {
        if (_statsBuffer) {
            glDeleteBuffers(1, &_statsBuffer);
            _statsBuffer = 0;
        }
    }

    void App::ResetStatsBuffer() {
        if (_statsBuffer == 0)
            return;
        StatsData zero {};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _statsBuffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(StatsData), &zero);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void App::LogDynamicParam(char const * name, float value) {
        spdlog::info("{}={:.3f}", name, value);
    }

    void App::RenderVolume(float deltaTime) {
        auto const volumeSize = _volumeData.GetVolumeSize();
        auto const volumeTex = _volumeData.GetVolumeTextureId();
        auto const windowSize = VCX::Engine::GetCurrentWindowSize();
        if (volumeSize == 0 || volumeTex == 0 || windowSize.first == 0 || windowSize.second == 0)
            return;

        ResetStatsBuffer();

        _time += deltaTime;

        auto const aspect = float(windowSize.first) / float(windowSize.second);
        auto const view = _camera.GetViewMatrix();
        auto const proj = _camera.GetProjectionMatrix(aspect);
        auto const invViewProj = glm::inverse(proj * view);
        auto const screenSize = glm::vec2(float(windowSize.first), float(windowSize.second));

        auto & uniforms = _volumeProgram.GetUniforms();
        uniforms.SetByName("uInvViewProj", invViewProj);
        uniforms.SetByName("uCameraPos", _camera.Eye);
        uniforms.SetByName("uScreenSize", screenSize);
        uniforms.SetByName("uTime", _time);
        uniforms.SetByName("uStepSize", _renderSettings.StepSize);
        uniforms.SetByName("uMaxSteps", _renderSettings.MaxSteps);
        uniforms.SetByName("uAlphaScale", _renderSettings.AlphaScale);
        uniforms.SetByName("uColorMode", static_cast<int>(_renderSettings.Mode));
        uniforms.SetByName("uEnableJitter", _renderSettings.EnableJitter ? 1 : 0);
        uniforms.SetByName("uJitterSeed", static_cast<float>(_frameIndex));
        uniforms.SetByName("uVolumeMin", kVolumeMin);
        uniforms.SetByName("uVolumeMax", kVolumeMax);
        uniforms.SetByName("uNoiseStrength", _dynamicSettings.NoiseStrength);
        uniforms.SetByName("uNoiseFreq", _dynamicSettings.NoiseFreq);
        uniforms.SetByName("uNoiseSpeed", _dynamicSettings.NoiseSpeed);
        uniforms.SetByName("uRippleAmp", _dynamicSettings.RippleAmp);
        uniforms.SetByName("uRippleFreq", _dynamicSettings.RippleFreq);
        uniforms.SetByName("uRippleSpeed", _dynamicSettings.RippleSpeed);

        if (_statsBuffer) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, _statsBuffer);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, volumeTex);

        {
            auto const progUse = _volumeProgram.Use();
            auto const vaoUse = _fullscreenVAO.Use();
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glBindTexture(GL_TEXTURE_3D, 0);
        if (_statsBuffer) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        }

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        if (_statsBuffer) {
            StatsData stats {};
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, _statsBuffer);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(stats), &stats);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            _accumulatedSteps += stats.Steps;
            _accumulatedRays += stats.Rays;
            _accumulatedEarly += stats.EarlyExit;
        }

        _statsTimer += deltaTime;
        if (_statsTimer >= 1.f) {
            if (_accumulatedRays > 0) {
                _statsSnapshot.AvgSteps = float(_accumulatedSteps) / float(_accumulatedRays);
                _statsSnapshot.EarlyExitRatio = float(_accumulatedEarly) / float(_accumulatedRays);
            } else {
                _statsSnapshot.AvgSteps = 0.f;
                _statsSnapshot.EarlyExitRatio = 0.f;
            }
            spdlog::info("Raymarch avg steps {:.1f}, early exit ratio {:.1f}%", _statsSnapshot.AvgSteps, _statsSnapshot.EarlyExitRatio * 100.f);
            _accumulatedSteps = 0;
            _accumulatedRays = 0;
            _accumulatedEarly = 0;
            _statsTimer = 0.f;
        }

        ++_frameIndex;
    }

    void App::OnFrame() {
        float const deltaTime = VCX::Engine::GetDeltaTime();
        _cameraManager.ProcessInput(_camera, ImGui::GetMousePos());
        _cameraManager.Update(_camera);
        RenderVolume(deltaTime);

        ImGui::Begin("Sphere Audio Visualizer");
        ImGui::Text("FPS: %.1f", CurrentFps());
        if (ImGui::Button("Reload Config")) {
            spdlog::info("Reload Config requested.");
        }
        ImGui::SliderFloat("alpha", &_alpha, 0.f, 1.f);
        ImGui::Separator();

        auto settings = _volumeData.GetSettings();
        bool settingsChanged = false;
        int volumeSizeInput = static_cast<int>(settings.VolumeSize);
        if (ImGui::InputInt("Volume Size", &volumeSizeInput)) {
            settings.VolumeSize = static_cast<std::size_t>(std::clamp(volumeSizeInput, 32, 256));
            settingsChanged = true;
        }

        int shellCount = settings.NumShells;
        if (ImGui::SliderInt("Shells", &shellCount, 1, 32)) {
            settings.NumShells = shellCount;
            settingsChanged = true;
        }

        float thickness = settings.Thickness;
        if (ImGui::SliderFloat("Thickness", &thickness, 0.02f, 0.5f)) {
            settings.Thickness = thickness;
            settingsChanged = true;
        }

        float gain = settings.GlobalGain;
        if (ImGui::SliderFloat("Global Gain", &gain, 0.1f, 5.f)) {
            settings.GlobalGain = gain;
            settingsChanged = true;
        }

        if (ImGui::Button("Regenerate")) {
            settingsChanged = true;
        }

        if (settingsChanged) {
            _volumeData.SetSettings(settings);
            _volumeData.Regenerate();
        }

        ImGui::Separator();
        ImGui::Text("Raymarch");
        ImGui::SliderFloat("Step Size", &_renderSettings.StepSize, 0.001f, 0.1f);
        ImGui::SliderInt("Max Steps", &_renderSettings.MaxSteps, 16, 512);
        ImGui::SliderFloat("Alpha Scale", &_renderSettings.AlphaScale, 0.1f, 10.f);
        ImGui::Checkbox("Enable Jitter", &_renderSettings.EnableJitter);
        int mode = static_cast<int>(_renderSettings.Mode);
        const char * colorModes[] = { "Grayscale", "Gradient" };
        if (ImGui::Combo("Color Mode", &mode, colorModes, IM_ARRAYSIZE(colorModes))) {
            _renderSettings.Mode = static_cast<ColorMode>(mode);
        }

        ImGui::Separator();
        ImGui::Text("Dynamics");
        if (ImGui::SliderFloat("Noise Strength", &_dynamicSettings.NoiseStrength, 0.f, 0.3f)) {
            LogDynamicParam("noiseStrength", _dynamicSettings.NoiseStrength);
        }
        if (ImGui::SliderFloat("Noise Frequency", &_dynamicSettings.NoiseFreq, 0.1f, 10.f)) {
            LogDynamicParam("noiseFreq", _dynamicSettings.NoiseFreq);
        }
        if (ImGui::SliderFloat("Noise Speed", &_dynamicSettings.NoiseSpeed, 0.f, 5.f)) {
            LogDynamicParam("noiseSpeed", _dynamicSettings.NoiseSpeed);
        }
        if (ImGui::SliderFloat("Ripple Amplitude", &_dynamicSettings.RippleAmp, 0.f, 0.3f)) {
            LogDynamicParam("rippleAmp", _dynamicSettings.RippleAmp);
        }
        if (ImGui::SliderFloat("Ripple Frequency", &_dynamicSettings.RippleFreq, 0.1f, 32.f)) {
            LogDynamicParam("rippleFreq", _dynamicSettings.RippleFreq);
        }
        if (ImGui::SliderFloat("Ripple Speed", &_dynamicSettings.RippleSpeed, 0.f, 6.f)) {
            LogDynamicParam("rippleSpeed", _dynamicSettings.RippleSpeed);
        }

        ImGui::Separator();
        auto const volumeSize = _volumeData.GetVolumeSize();
        if (volumeSize > 0) {
            int sliceIndex = static_cast<int>(_volumeData.GetSliceIndex());
            if (ImGui::SliderInt("Slice Z", &sliceIndex, 0, static_cast<int>(volumeSize) - 1)) {
                _volumeData.SetSliceIndex(static_cast<std::size_t>(sliceIndex));
            }

            ImGui::Text("Volume tex ID: %u", _volumeData.GetVolumeTextureId());
            ImGui::Text("Slice Preview");
            auto const previewSize = ImVec2(256.f, 256.f);
            ImGui::Image(
                _volumeData.GetSliceTextureHandle(),
                previewSize,
                ImVec2(0.f, 1.f),
                ImVec2(1.f, 0.f));
        }

        ImGui::Separator();
        ImGui::Text("Camera Controls");
        glm::vec3 cameraTarget = _camera.Target;
        Engine::Spherical spherical(_camera.Eye - _camera.Target);
        bool cameraChanged = false;
        cameraChanged |= ImGui::DragFloat3("Target", &cameraTarget.x, 0.01f);
        float distance = spherical.Radius;
        cameraChanged |= ImGui::DragFloat("Distance", &distance, 0.01f, 0.01f, 100.f);
        float azimuth = glm::degrees(spherical.Theta);
        cameraChanged |= ImGui::SliderFloat("Azimuth", &azimuth, -180.f, 180.f);
        float polar = glm::degrees(spherical.Phi);
        cameraChanged |= ImGui::SliderFloat("Polar", &polar, 0.1f, 179.9f);
        if (cameraChanged) {
            _camera.Target = cameraTarget;
            Engine::Spherical updated;
            updated.Radius = std::max(0.01f, distance);
            updated.Theta = glm::radians(azimuth);
            updated.Phi = glm::radians(polar);
            _camera.Eye = _camera.Target + updated.Vec();
            _cameraManager.Save(_camera);
            _cameraManager.Reset(_camera);
        }

        ImGui::Text("Avg steps: %.1f", _statsSnapshot.AvgSteps);
        ImGui::Text("Early exit ratio: %.1f%%", _statsSnapshot.EarlyExitRatio * 100.f);

        ImGui::End();
    }

    int RunApp() {
        SetupLogger();
        spdlog::info("Starting SphereAudioVisualizer app.");
        return VCX::Engine::RunApp<App>(VCX::Engine::AppContextOptions {
            .Title      = "VCX: Sphere Audio Visualizer",
            .WindowSize = { 1280, 720 },
            .FontSize   = 16,

            .IconFileNames = VCX::Assets::DefaultIcons,
            .FontFileNames = VCX::Assets::DefaultFonts,
        });
    }
} // namespace VCX::Apps::SphereAudioVisualizer
