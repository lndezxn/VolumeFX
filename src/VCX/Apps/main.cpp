#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "Apps/SphereAudioVisualizer/App.hpp"

namespace {
    using AppRunner = std::function<int()>;

    std::string ParseSelectedApp(int const argc, char * argv[]) {
        std::string selected { "spherevis" };
        for (int i = 1; i < argc; ++i) {
            std::string_view const arg { argv[i] ? argv[i] : "" };
            constexpr std::string_view prefix { "--app=" };
            if (arg == "--app" && i + 1 < argc) {
                selected = argv[++i];
            } else if (arg.rfind(prefix, 0) == 0) {
                selected = std::string(arg.substr(prefix.size()));
            }
        }
        return selected;
    }
}

int main(int argc, char * argv[]) {
    VCX::Apps::SphereAudioVisualizer::EnsureLogger();

    std::unordered_map<std::string, AppRunner> registry;
    registry.emplace("spherevis", &VCX::Apps::SphereAudioVisualizer::RunApp);
    registry.emplace("volumefx", [] {
        spdlog::error("VolumeFX app is not available in this build.");
        return 1;
    });

    auto selected = ParseSelectedApp(argc, argv);
    if (auto it = registry.find(selected); it != registry.end()) {
        spdlog::info("Launching app '{}'", it->first);
        return it->second();
    }

    spdlog::warn("Unknown app '{}', defaulting to spherevis", selected);
    return registry.at("spherevis")();
}
