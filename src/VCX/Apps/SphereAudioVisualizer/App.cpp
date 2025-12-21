#include "Apps/SphereAudioVisualizer/App.hpp"

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
    } // namespace

    void EnsureLogger() {
        SetupLogger();
    }

    App::App(): _alpha(0.5f) {
        SetupLogger();
        spdlog::debug("SphereAudioVisualizer initialized.");
    }

    void App::OnFrame() {
        ImGui::Begin("Sphere Audio Visualizer");
        ImGui::Text("FPS: %.1f", CurrentFps());
        if (ImGui::Button("Reload Config")) {
            spdlog::info("Reload Config requested.");
        }
        ImGui::SliderFloat("alpha", &_alpha, 0.f, 1.f);
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
