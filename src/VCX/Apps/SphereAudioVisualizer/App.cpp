#include "Apps/SphereAudioVisualizer/App.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <vector>

#include <imgui.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Assets/bundled.h"

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
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

    App::App(): _alpha(0.5f) {
        SetupLogger();
        spdlog::debug("SphereAudioVisualizer initialized.");
        _volumeData.Regenerate();
    }

    void App::OnFrame() {
        ImGui::Begin("Sphere Audio Visualizer");
        ImGui::Text("FPS: %.1f", CurrentFps());
        if (ImGui::Button("Reload Config")) {
            spdlog::info("Reload Config requested.");
        }
        ImGui::SliderFloat("alpha", &_alpha, 0.f, 1.f);
        ImGui::Separator();

        auto settings = _volumeData.GetSettings();
        bool       settingsChanged = false;
        int        volumeSizeInput = static_cast<int>(settings.VolumeSize);
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

        bool requestRebuild = false;
        if (ImGui::Button("Regenerate")) {
            requestRebuild = true;
        }

        if (settingsChanged || requestRebuild) {
            _volumeData.SetSettings(settings);
            _volumeData.Regenerate();
        }

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
