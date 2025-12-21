#include "Apps/SphereAudioVisualizer/App.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <yaml-cpp/yaml.h>

#include "Assets/bundled.h"
#include "Engine/app.h"
#include "Engine/math.hpp"
#include "Engine/GL/Sampler.hpp"

namespace VCX::Apps::SphereAudioVisualizer {
    namespace {
        constexpr auto kFullScreenTriangle = std::array<float, 6> {
            -1.f, -1.f,
             3.f, -1.f,
            -1.f,  3.f,
        };

        constexpr glm::vec3 kVolumeMin { -1.f };
        constexpr glm::vec3 kVolumeMax {  1.f };
        constexpr std::size_t kTransferLutSize = 256;

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

        int ClampFftIndex(int idx) {
            return std::clamp(idx, 0, static_cast<int>(App::kFftSizes.size()) - 1);
        }

        int CurrentFftSize(App::AudioAnalysisSettings const & settings) {
            return App::kFftSizes[ClampFftIndex(settings.FftSizeIndex)];
        }

        void BuildWindowCoeffs(std::vector<float> & coeffs, int fftSize, App::WindowType type) {
            coeffs.resize(static_cast<std::size_t>(fftSize));
            if (fftSize <= 1) {
                std::fill(coeffs.begin(), coeffs.end(), 1.f);
                return;
            }
            float denom = static_cast<float>(fftSize - 1);
            float twoPi = 6.28318530718f;
            for (int i = 0; i < fftSize; ++i) {
                float phase = twoPi * static_cast<float>(i) / denom;
                switch (type) {
                case App::WindowType::Hamming:
                    coeffs[static_cast<std::size_t>(i)] = 0.54f - 0.46f * std::cos(phase);
                    break;
                case App::WindowType::Hann:
                default:
                    coeffs[static_cast<std::size_t>(i)] = 0.5f * (1.f - std::cos(phase));
                    break;
                }
            }
        }

        void ApplyWindow(std::vector<float> const & coeffs, std::vector<float> & samples, int fftSize) {
            if (coeffs.size() < static_cast<std::size_t>(fftSize) || samples.size() < static_cast<std::size_t>(fftSize))
                return;
            for (int i = 0; i < fftSize; ++i) {
                samples[static_cast<std::size_t>(i)] *= coeffs[static_cast<std::size_t>(i)];
            }
        }

        void DownsampleSpectrum(std::vector<float> const & src, std::vector<float> & dst, std::size_t target) {
            if (target == 0 || src.empty()) {
                dst.clear();
                return;
            }
            dst.assign(target, 0.f);
            float step = static_cast<float>(src.size()) / static_cast<float>(target);
            for (std::size_t i = 0; i < target; ++i) {
                std::size_t start = static_cast<std::size_t>(std::floor(step * i));
                std::size_t end = static_cast<std::size_t>(std::floor(step * (i + 1)));
                end = std::min(end, src.size());
                start = std::min(start, src.size());
                if (end <= start) continue;
                float sum = 0.f;
                for (std::size_t j = start; j < end; ++j) {
                    sum += src[j];
                }
                dst[i] = sum / static_cast<float>(end - start);
            }
        }

        struct BandRange {
            int Start;
            int End; // exclusive
        };

        BandRange ComputeBandRange(App::AudioAnalysisSettings const & settings, int bandIndex, int numBands, int fftSize, int sampleRate) {
            int half = fftSize / 2;
            if (half <= 1) return { 0, 1 };
            bandIndex = std::clamp(bandIndex, 0, numBands - 1);
            if (settings.Mapping == App::MappingType::Linear) {
                int start = static_cast<int>(std::floor(static_cast<float>(bandIndex) * half / numBands));
                int end   = static_cast<int>(std::floor(static_cast<float>(bandIndex + 1) * half / numBands));
                end = std::max(end, start + 1);
                end = std::min(end, half);
                return { start, end };
            }

            float nyquist = static_cast<float>(sampleRate) * 0.5f;
            float fMin = std::max(settings.MinFrequency, 1.f);
            float fMax = std::max(fMin, nyquist);
            float logMin = std::log(fMin);
            float logMax = std::log(fMax);
            float t0 = static_cast<float>(bandIndex) / static_cast<float>(numBands);
            float t1 = static_cast<float>(bandIndex + 1) / static_cast<float>(numBands);
            float f0 = std::exp(logMin + (logMax - logMin) * t0);
            float f1 = std::exp(logMin + (logMax - logMin) * t1);
            int start = static_cast<int>(std::floor(f0 * static_cast<float>(fftSize) / static_cast<float>(sampleRate)));
            int end   = static_cast<int>(std::ceil (f1 * static_cast<float>(fftSize) / static_cast<float>(sampleRate)));
            start = std::clamp(start, 0, half - 1);
            end   = std::clamp(end, start + 1, half);
            return { start, end };
        }

        float AggregateBand(std::vector<float> const & spectrum, BandRange range, App::AggregateType agg) {
            if (range.End <= range.Start || spectrum.empty()) return 0.f;
            range.Start = std::clamp(range.Start, 0, static_cast<int>(spectrum.size()));
            range.End = std::clamp(range.End, 0, static_cast<int>(spectrum.size()));
            float value = 0.f;
            if (agg == App::AggregateType::Max) {
                for (int i = range.Start; i < range.End; ++i) {
                    value = std::max(value, spectrum[static_cast<std::size_t>(i)]);
                }
            } else {
                float sum = 0.f;
                for (int i = range.Start; i < range.End; ++i) {
                    sum += spectrum[static_cast<std::size_t>(i)];
                }
                value = sum / static_cast<float>(range.End - range.Start);
            }
            return value;
        }

        float ApplyCompression(float magnitude, float k) {
            k = std::max(k, 0.f);
            return std::log1p(k * magnitude);
        }

        float UpdateAgcGain(float currentGain, float level, App::AudioAnalysisSettings const & settings, float deltaTime) {
            float target = (level > 1e-6f) ? settings.AgcTarget / level : settings.AgcMaxGain;
            target = std::clamp(target, 1.f / settings.AgcMaxGain, settings.AgcMaxGain);
            float tau = target > currentGain ? settings.AgcAttack : settings.AgcRelease;
            tau = std::max(tau, 1e-3f);
            float alpha = std::exp(-deltaTime / tau);
            float updated = alpha * currentGain + (1.f - alpha) * target;
            return std::clamp(updated, 1.f / settings.AgcMaxGain, settings.AgcMaxGain);
        }

        char const * WindowTypeName(App::WindowType type) {
            switch (type) {
            case App::WindowType::Hamming:
                return "Hamming";
            case App::WindowType::Hann:
            default:
                return "Hann";
            }
        }

        bool TryParseWindowType(std::string const & value, App::WindowType & out) {
            if (value == "Hamming") {
                out = App::WindowType::Hamming;
                return true;
            }
            if (value == "Hann") {
                out = App::WindowType::Hann;
                return true;
            }
            return false;
        }

        char const * MappingTypeName(App::MappingType type) {
            switch (type) {
            case App::MappingType::Log:
                return "Log";
            case App::MappingType::Linear:
            default:
                return "Linear";
            }
        }

        bool TryParseMappingType(std::string const & value, App::MappingType & out) {
            if (value == "Log") {
                out = App::MappingType::Log;
                return true;
            }
            if (value == "Linear") {
                out = App::MappingType::Linear;
                return true;
            }
            return false;
        }

        std::string TransferPresetName(App::TransferPreset preset) {
            switch (preset) {
            case App::TransferPreset::Neon:
                return "Neon";
            case App::TransferPreset::Heatmap:
                return "Heatmap";
            case App::TransferPreset::Smoke:
            default:
                return "Smoke";
            }
        }

        bool TryParseTransferPreset(std::string const & value, App::TransferPreset & out) {
            if (value == "Neon") {
                out = App::TransferPreset::Neon;
                return true;
            }
            if (value == "Heatmap") {
                out = App::TransferPreset::Heatmap;
                return true;
            }
            if (value == "Smoke") {
                out = App::TransferPreset::Smoke;
                return true;
            }
            return false;
        }

        glm::vec3 NodeToVec3(YAML::Node const & node, glm::vec3 const & fallback) {
            if (!node || !node.IsSequence() || node.size() < 3) {
                return fallback;
            }
            return glm::vec3(node[0].as<float>(fallback.r), node[1].as<float>(fallback.g), node[2].as<float>(fallback.b));
        }
    }

    char const * App::ColorModeName(App::ColorMode mode) {
        switch (mode) {
        case App::ColorMode::Gradient:
            return "TransferLUT";
        case App::ColorMode::Gray:
        default:
            return "Grayscale";
        }
    }

    bool App::TryParseColorMode(std::string const & value, App::ColorMode & out) {
        if (value == "TransferLUT") {
            out = App::ColorMode::Gradient;
            return true;
        }
        if (value == "Grayscale") {
            out = App::ColorMode::Gray;
            return true;
        }
        return false;
    }

    void EnsureLogger() {
        SetupLogger();
    }

    std::filesystem::path App::ConfigFilePath() const {
        return std::filesystem::current_path() / "SphereVisConfig.yaml";
    }

    void App::LoadConfig() {
        auto const path = ConfigFilePath();
        if (!std::filesystem::exists(path)) {
            spdlog::info("Config {} missing, using defaults.", path.string());
            ApplyTransferPreset(_transferPreset);
            _transferDirty = true;
            _volumeData.Regenerate();
            return;
        }

        try {
            auto const root = YAML::LoadFile(path.string());

            auto const updateString = [&]<std::size_t N>(YAML::Node const & node, char (&dst)[N]) {
                if (!node) {
                    dst[0] = '\0';
                    return;
                }
                auto const str = node.as<std::string>("");
                std::strncpy(dst, str.c_str(), N);
                dst[N - 1] = '\0';
            };

            updateString(root["lastAudioPath"], _audioPath);
            if (_audioPath[0] != '\0') {
                if (!_audio.LoadFile(_audioPath)) {
                    spdlog::warn("Failed to load audio from config path {}", _audioPath);
                }
            }

            if (auto audioNode = root["audio"]) {
                _audioLoop = audioNode["loop"].as<bool>(_audioLoop);
                _audio.SetLoop(_audioLoop);
                _monoMixMode = audioNode["monoMixMode"].as<bool>(_monoMixMode);
                _audio.SetMonoMixMode(_monoMixMode);
            }

            if (auto analysisNode = root["analysis"]) {
                if (auto fftNode = analysisNode["fftSize"]) {
                    int const fftSize = fftNode.as<int>(CurrentFftSize(_analysisSettings));
                    auto const iter = std::find(kFftSizes.begin(), kFftSizes.end(), fftSize);
                    if (iter != kFftSizes.end()) {
                        _analysisSettings.FftSizeIndex = static_cast<int>(std::distance(kFftSizes.begin(), iter));
                    }
                }
                _analysisSettings.NumBands = analysisNode["numBands"].as<int>(_analysisSettings.NumBands);
                _analysisSettings.NumBands = std::clamp(_analysisSettings.NumBands, 1, 256);
                if (auto windowNode = analysisNode["windowType"]) {
                    App::WindowType type;
                    if (TryParseWindowType(windowNode.as<std::string>(), type)) {
                        _analysisSettings.Window = type;
                    }
                }
                if (auto mappingNode = analysisNode["mappingType"]) {
                    App::MappingType mapping;
                    if (TryParseMappingType(mappingNode.as<std::string>(), mapping)) {
                        _analysisSettings.Mapping = mapping;
                    }
                }
                _analysisSettings.CompressK = analysisNode["compressK"].as<float>(_analysisSettings.CompressK);
                if (auto agcNode = analysisNode["agc"]) {
                    _analysisSettings.AgcEnabled = agcNode["enabled"].as<bool>(_analysisSettings.AgcEnabled);
                    _analysisSettings.AgcTarget = agcNode["target"].as<float>(_analysisSettings.AgcTarget);
                    _analysisSettings.AgcAttack = agcNode["attack"].as<float>(_analysisSettings.AgcAttack);
                    _analysisSettings.AgcRelease = agcNode["release"].as<float>(_analysisSettings.AgcRelease);
                    _analysisSettings.AgcMaxGain = agcNode["maxGain"].as<float>(_analysisSettings.AgcMaxGain);
                }
            }

            if (auto volumeNode = root["volume"]) {
                auto settings = _volumeData.GetSettings();
                settings.VolumeSize = static_cast<std::size_t>(volumeNode["volumeSize"].as<int>(static_cast<int>(settings.VolumeSize)));
                settings.VolumeSize = std::clamp(settings.VolumeSize, std::size_t(32), std::size_t(256));
                settings.AmpScale = volumeNode["ampScale"].as<float>(settings.AmpScale);
                settings.ThicknessScale = volumeNode["thicknessScale"].as<float>(settings.ThicknessScale);
                _volumeData.SetSettings(settings);
            }

            if (auto renderNode = root["render"]) {
                _renderSettings.StepSize = renderNode["stepSize"].as<float>(_renderSettings.StepSize);
                _renderSettings.MaxSteps = renderNode["maxSteps"].as<int>(_renderSettings.MaxSteps);
                _renderSettings.MaxSteps = std::clamp(_renderSettings.MaxSteps, 16, 512);
                _renderSettings.AlphaScale = renderNode["alphaScale"].as<float>(_renderSettings.AlphaScale);
                _renderSettings.AlphaScale = std::clamp(_renderSettings.AlphaScale, 0.1f, 10.f);
            }

            if (auto dynamicNode = root["dynamic"]) {
                _dynamicSettings.NoiseStrength = dynamicNode["noiseStrength"].as<float>(_dynamicSettings.NoiseStrength);
                _dynamicSettings.NoiseFreq = dynamicNode["noiseFreq"].as<float>(_dynamicSettings.NoiseFreq);
                _dynamicSettings.NoiseSpeed = dynamicNode["noiseSpeed"].as<float>(_dynamicSettings.NoiseSpeed);
                _dynamicSettings.RippleAmp = dynamicNode["rippleAmp"].as<float>(_dynamicSettings.RippleAmp);
                _dynamicSettings.RippleFreq = dynamicNode["rippleFreq"].as<float>(_dynamicSettings.RippleFreq);
                _dynamicSettings.RippleSpeed = dynamicNode["rippleSpeed"].as<float>(_dynamicSettings.RippleSpeed);
            }

            if (auto transferNode = root["transferFunction"]) {
                if (auto presetNode = transferNode["preset"]) {
                    App::TransferPreset preset;
                    if (TryParseTransferPreset(presetNode.as<std::string>(), preset)) {
                        _transferPreset = preset;
                    }
                }
                _transferSettings.LowThreshold = transferNode["lowThreshold"].as<float>(_transferSettings.LowThreshold);
                _transferSettings.HighThreshold = transferNode["highThreshold"].as<float>(_transferSettings.HighThreshold);
                _transferSettings.Gamma = transferNode["gamma"].as<float>(_transferSettings.Gamma);
                _transferSettings.OverallAlpha = transferNode["overallAlpha"].as<float>(_transferSettings.OverallAlpha);
                if (auto controlPoints = transferNode["controlPoints"]) {
                    for (std::size_t i = 0; i < _transferSettings.ControlPoints.size() && i < controlPoints.size(); ++i) {
                        auto & dst = _transferSettings.ControlPoints[i];
                        auto const & src = controlPoints[static_cast<int>(i)];
                        dst.Position = src["position"].as<float>(dst.Position);
                        dst.Color = NodeToVec3(src["color"], dst.Color);
                        dst.Alpha = src["alpha"].as<float>(dst.Alpha);
                    }
                }
            }

            _transferDirty = true;
            _volumeData.Regenerate();
            spdlog::info("Config loaded from {} (audio '{}', fft {}, bands {}, preset {})",
                path.string(),
                _audioPath,
                CurrentFftSize(_analysisSettings),
                _analysisSettings.NumBands,
                TransferPresetName(_transferPreset));
        } catch (YAML::Exception const & ex) {
            spdlog::error("Config load failed {}: {}", path.string(), ex.what());
            ApplyTransferPreset(_transferPreset);
            _transferDirty = true;
            _volumeData.Regenerate();
        }
    }

    void App::SaveConfig() {
        auto const path = ConfigFilePath();
        try {
            if (auto const parent = path.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
                std::filesystem::create_directories(parent);
            }
            YAML::Node root;
            root["lastAudioPath"] = std::string(_audioPath);
            YAML::Node audioNode;
            audioNode["loop"] = _audioLoop;
            audioNode["monoMixMode"] = _monoMixMode;
            root["audio"] = audioNode;

            YAML::Node analysisNode;
            analysisNode["fftSize"] = CurrentFftSize(_analysisSettings);
            analysisNode["numBands"] = _analysisSettings.NumBands;
            analysisNode["windowType"] = WindowTypeName(_analysisSettings.Window);
            analysisNode["mappingType"] = MappingTypeName(_analysisSettings.Mapping);
            analysisNode["compressK"] = _analysisSettings.CompressK;
            YAML::Node agcNode;
            agcNode["enabled"] = _analysisSettings.AgcEnabled;
            agcNode["target"] = _analysisSettings.AgcTarget;
            agcNode["attack"] = _analysisSettings.AgcAttack;
            agcNode["release"] = _analysisSettings.AgcRelease;
            agcNode["maxGain"] = _analysisSettings.AgcMaxGain;
            analysisNode["agc"] = agcNode;
            root["analysis"] = analysisNode;

            YAML::Node volumeNode;
            auto const volumeSettings = _volumeData.GetSettings();
            volumeNode["volumeSize"] = static_cast<int>(volumeSettings.VolumeSize);
            volumeNode["ampScale"] = volumeSettings.AmpScale;
            volumeNode["thicknessScale"] = volumeSettings.ThicknessScale;
            root["volume"] = volumeNode;

            YAML::Node renderNode;
            renderNode["stepSize"] = _renderSettings.StepSize;
            renderNode["maxSteps"] = _renderSettings.MaxSteps;
            renderNode["alphaScale"] = _renderSettings.AlphaScale;
            root["render"] = renderNode;

            YAML::Node dynamicNode;
            dynamicNode["noiseStrength"] = _dynamicSettings.NoiseStrength;
            dynamicNode["noiseFreq"] = _dynamicSettings.NoiseFreq;
            dynamicNode["noiseSpeed"] = _dynamicSettings.NoiseSpeed;
            dynamicNode["rippleAmp"] = _dynamicSettings.RippleAmp;
            dynamicNode["rippleFreq"] = _dynamicSettings.RippleFreq;
            dynamicNode["rippleSpeed"] = _dynamicSettings.RippleSpeed;
            root["dynamic"] = dynamicNode;

            YAML::Node transferNode;
            transferNode["preset"] = TransferPresetName(_transferPreset);
            transferNode["lowThreshold"] = _transferSettings.LowThreshold;
            transferNode["highThreshold"] = _transferSettings.HighThreshold;
            transferNode["gamma"] = _transferSettings.Gamma;
            transferNode["overallAlpha"] = _transferSettings.OverallAlpha;
            YAML::Node controlPoints;
            for (auto const & point : _transferSettings.ControlPoints) {
                YAML::Node cp;
                cp["position"] = point.Position;
                YAML::Node color;
                color.push_back(point.Color.r);
                color.push_back(point.Color.g);
                color.push_back(point.Color.b);
                cp["color"] = color;
                cp["alpha"] = point.Alpha;
                controlPoints.push_back(cp);
            }
            transferNode["controlPoints"] = controlPoints;
            root["transferFunction"] = transferNode;

            std::ofstream out(path);
            out << root;
            spdlog::info("Config saved to {} (audio '{}', fft {}, bands {}, preset {})",
                path.string(),
                _audioPath,
                CurrentFftSize(_analysisSettings),
                _analysisSettings.NumBands,
                TransferPresetName(_transferPreset));
        } catch (std::exception const & ex) {
            spdlog::error("Config save failed {}: {}", path.string(), ex.what());
        }
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
        _transferLutTexture.SetUnit(1);
        _volumeProgram.GetUniforms().SetByName("uTransferLut", 1);
        _audio.SetMonoMixMode(_monoMixMode);
        LoadConfig();
    }

    App::~App() {
        if (_fftCfg) {
            kiss_fft_free(_fftCfg);
            _fftCfg = nullptr;
        }
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

    void App::RenderAudioUI() {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Audio");
            ImGui::InputText("File", _audioPath, IM_ARRAYSIZE(_audioPath));
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                bool ok = _audio.LoadFile(_audioPath);
                if (ok) {
                    spdlog::info("Audio loaded: {}", _audioPath);
                } else {
                    spdlog::error("Audio load failed: {}", _audio.GetLastError());
                }
            }

            if (ImGui::Button("Play")) {
                _audio.Play();
                spdlog::info("Audio play");
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause")) {
                _audio.Pause();
                spdlog::info("Audio pause");
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                _audio.Stop();
                spdlog::info("Audio stop");
            }

            if (ImGui::Checkbox("Loop", &_audioLoop)) {
                _audio.SetLoop(_audioLoop);
            }
            if (ImGui::Checkbox("Mono Mix", &_monoMixMode)) {
                _audio.SetMonoMixMode(_monoMixMode);
            }

            float timeNow = _audio.GetTimeSeconds();
            float duration = _audio.GetDurationSeconds();
            ImGui::Text("Time: %.2f / %.2f s", timeNow, duration);
            ImGui::Text("Rate: %u Hz, Channels: %u", _audio.GetSampleRate(), _audio.GetChannels());
            int maxHeadroom = _fftSize * 2;
            if (ImGui::SliderInt("Headroom", &_audioHeadroom, 0, maxHeadroom)) {
                _audioHeadroom = std::clamp(_audioHeadroom, 0, maxHeadroom);
            }
            float fill = _audio.GetRingFillRatio();
            ImGui::ProgressBar(fill, ImVec2(-1.f, 0.f), "Ring fill");
            ImGui::Text("Ring strategy: overwrite-old");
            ImGui::Text("Readable: %zu", _audioReadable);
            ImGui::Text("FFT updates/s: %zu", _fftUpdatesPerSecond);
            ImGui::Text("Window RMS: %.5f", _audioWindowRms);
            ImGui::Text("FFT size: %d", _fftSize);
            ImGui::Text("FFT time: %.3f ms", _analysisState.LastFftMs);
            ImGui::Text("Energies min/max/avg: %.3f / %.3f / %.3f",
                _analysisState.EnergyMin,
                _analysisState.EnergyMax,
                _analysisState.EnergyAvg);
            ImGui::Text("AGC gain: %.3f", _analysisState.AgcGain);
            ImGui::PlotLines("Oscilloscope",
                _oscilloscopePoints.data(),
                static_cast<int>(_oscilloscopePoints.size()),
                0,
                nullptr,
                -1.f,
                1.f,
                ImVec2(-1.f, 80.f));
            ImGui::Text("overrunWrites: %llu, droppedSamples: %llu, underrunReads: %llu",
                static_cast<unsigned long long>(_audio.GetOverrunWrites()),
                static_cast<unsigned long long>(_audio.GetDroppedSamples()),
                static_cast<unsigned long long>(_audio.GetUnderrunReads()));
            if (!_audio.GetLastError().empty()) {
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", _audio.GetLastError().c_str());
            }
            if (_audio.UsingSineFallback()) {
                ImGui::Text("Fallback: sine test (load failed)");
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("FFT / Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("FFT / Analysis");
            const char * fftSizeLabels[] = { "512", "1024", "2048", "4096" };
            if (ImGui::Combo("FFT Size", &_analysisSettings.FftSizeIndex, fftSizeLabels, IM_ARRAYSIZE(fftSizeLabels))) {
                _analysisSettings.FftSizeIndex = ClampFftIndex(_analysisSettings.FftSizeIndex);
            }

            const char * windowNames[] = { "Hann", "Hamming" };
            int windowType = static_cast<int>(_analysisSettings.Window);
            if (ImGui::Combo("Window", &windowType, windowNames, IM_ARRAYSIZE(windowNames))) {
                _analysisSettings.Window = static_cast<WindowType>(windowType);
            }

            const char * mappingNames[] = { "Linear", "Log" };
            int mappingType = static_cast<int>(_analysisSettings.Mapping);
            if (ImGui::Combo("Mapping", &mappingType, mappingNames, IM_ARRAYSIZE(mappingNames))) {
                _analysisSettings.Mapping = static_cast<MappingType>(mappingType);
            }

            const char * aggregateNames[] = { "Average", "Max" };
            int aggregateType = static_cast<int>(_analysisSettings.Aggregate);
            if (ImGui::Combo("Band Aggregate", &aggregateType, aggregateNames, IM_ARRAYSIZE(aggregateNames))) {
                _analysisSettings.Aggregate = static_cast<AggregateType>(aggregateType);
            }

            const char * bandOptions[] = { "8", "16", "32" };
            int bandIndex = (_analysisSettings.NumBands == 8) ? 0 : (_analysisSettings.NumBands == 32 ? 2 : 1);
            if (ImGui::Combo("Bands", &bandIndex, bandOptions, IM_ARRAYSIZE(bandOptions))) {
                _analysisSettings.NumBands = bandIndex == 0 ? 8 : (bandIndex == 2 ? 32 : 16);
            }

            ImGui::SliderFloat("Min Freq (Hz)", &_analysisSettings.MinFrequency, 1.f, std::max(1.f, _audio.GetSampleRate() * 0.5f));
            ImGui::SliderFloat("Compress k", &_analysisSettings.CompressK, 0.f, 32.f);
            ImGui::Checkbox("Show Spectrum", &_analysisSettings.ShowSpectrum);

            bool agcEnabled = _analysisSettings.AgcEnabled;
            if (ImGui::Checkbox("AGC Enabled", &agcEnabled)) {
                _analysisSettings.AgcEnabled = agcEnabled;
            }
            ImGui::SliderFloat("AGC Target", &_analysisSettings.AgcTarget, 0.05f, 2.f);
            ImGui::SliderFloat("AGC Attack (s)", &_analysisSettings.AgcAttack, 0.01f, 1.f);
            ImGui::SliderFloat("AGC Release (s)", &_analysisSettings.AgcRelease, 0.05f, 2.f);
            ImGui::SliderFloat("AGC Max Gain", &_analysisSettings.AgcMaxGain, 1.f, 40.f);

            if (!_analysisState.BandEnergies.empty()) {
                ImGui::PlotHistogram("Energies",
                    _analysisState.BandEnergies.data(),
                    static_cast<int>(_analysisState.BandEnergies.size()),
                    0,
                    nullptr,
                    0.f,
                    1.f,
                    ImVec2(-1.f, 120.f));
            }
            if (_analysisSettings.ShowSpectrum && !_analysisState.SpectrumDownsample.empty()) {
                ImGui::PlotLines("Spectrum",
                    _analysisState.SpectrumDownsample.data(),
                    static_cast<int>(_analysisState.SpectrumDownsample.size()),
                    0,
                    nullptr,
                    0.f,
                    0.1f,
                    ImVec2(-1.f, 80.f));
            }
        }
    }

    void App::RenderTransferFunctionUI() {
        if (!ImGui::CollapsingHeader("Transfer Function", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        const char * presetNames[] = { "Smoke", "Neon", "Heatmap" };
        int presetIndex = static_cast<int>(_transferPreset);
        if (ImGui::Combo("Preset", &presetIndex, presetNames, IM_ARRAYSIZE(presetNames))) {
            presetIndex = std::clamp(presetIndex, 0, static_cast<int>(TransferPreset::Heatmap));
            _transferPreset = static_cast<TransferPreset>(presetIndex);
            ApplyTransferPreset(_transferPreset);
        }

        bool settingsChanged = false;
        float lowValue = _transferSettings.LowThreshold;
        if (ImGui::SliderFloat("Low Threshold", &lowValue, 0.f, 1.f)) {
            _transferSettings.LowThreshold = std::min(lowValue, _transferSettings.HighThreshold);
            settingsChanged = true;
        }
        float highValue = _transferSettings.HighThreshold;
        if (ImGui::SliderFloat("High Threshold", &highValue, 0.f, 1.f)) {
            _transferSettings.HighThreshold = std::max(highValue, _transferSettings.LowThreshold);
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Gamma", &_transferSettings.Gamma, 0.1f, 4.f)) {
            settingsChanged = true;
        }
        if (ImGui::SliderFloat("Overall Alpha", &_transferSettings.OverallAlpha, 0.f, 1.f)) {
            settingsChanged = true;
        }

        for (int i = 0; i < static_cast<int>(_transferSettings.ControlPoints.size()); ++i) {
            auto & point = _transferSettings.ControlPoints[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            if (ImGui::ColorEdit3("Color", glm::value_ptr(point.Color))) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Alpha", &point.Alpha, 0.f, 1.f)) {
                settingsChanged = true;
            }
            ImGui::PopID();
        }

        if (settingsChanged) {
            _transferDirty = true;
        }
    }

    void App::ApplyTransferPreset(TransferPreset preset) {
        _transferPreset = preset;
        auto & settings = _transferSettings;
        switch (preset) {
        case TransferPreset::Smoke:
            settings.LowThreshold = 0.f;
            settings.HighThreshold = 1.f;
            settings.Gamma = 1.2f;
            settings.OverallAlpha = 0.7f;
            settings.ControlPoints = std::array<TransferControlPoint, 4> {
                TransferControlPoint{0.f, glm::vec3(0.05f, 0.05f, 0.07f), 0.05f},
                TransferControlPoint{0.35f, glm::vec3(0.2f, 0.2f, 0.25f), 0.25f},
                TransferControlPoint{0.7f, glm::vec3(0.6f, 0.45f, 0.3f), 0.65f},
                TransferControlPoint{1.f, glm::vec3(0.95f, 0.85f, 0.4f), 1.f},
            };
            break;
        case TransferPreset::Neon:
            settings.LowThreshold = 0.f;
            settings.HighThreshold = 0.95f;
            settings.Gamma = 0.85f;
            settings.OverallAlpha = 1.f;
            settings.ControlPoints = std::array<TransferControlPoint, 4> {
                TransferControlPoint{0.f, glm::vec3(0.05f, 0.12f, 0.35f), 0.1f},
                TransferControlPoint{0.35f, glm::vec3(0.05f, 0.8f, 0.95f), 0.75f},
                TransferControlPoint{0.7f, glm::vec3(0.95f, 0.05f, 0.9f), 0.85f},
                TransferControlPoint{1.f, glm::vec3(1.f, 0.8f, 0.35f), 1.f},
            };
            break;
        case TransferPreset::Heatmap:
            settings.LowThreshold = 0.05f;
            settings.HighThreshold = 1.f;
            settings.Gamma = 1.4f;
            settings.OverallAlpha = 0.95f;
            settings.ControlPoints = std::array<TransferControlPoint, 4> {
                TransferControlPoint{0.f, glm::vec3(0.15f, 0.15f, 0.45f), 0.12f},
                TransferControlPoint{0.33f, glm::vec3(0.15f, 0.65f, 0.2f), 0.45f},
                TransferControlPoint{0.66f, glm::vec3(0.95f, 0.65f, 0.05f), 0.75f},
                TransferControlPoint{1.f, glm::vec3(0.8f, 0.1f, 0.02f), 1.f},
            };
            break;
        }
        _transferDirty = true;
    }

    glm::vec4 App::EvaluateTransferFunction(float sample) const {
        float normalized = std::clamp(sample, 0.f, 1.f);
        float range = _transferSettings.HighThreshold - _transferSettings.LowThreshold;
        if (range > 0.f) {
            normalized = (normalized - _transferSettings.LowThreshold) / range;
            normalized = std::clamp(normalized, 0.f, 1.f);
        }
        float gamma = std::max(_transferSettings.Gamma, 0.01f);
        normalized = normalized > 0.f ? std::pow(normalized, gamma) : 0.f;

        auto const & points = _transferSettings.ControlPoints;
        if (points.empty()) {
            return glm::vec4(normalized, normalized, normalized, normalized);
        }

        auto const & first = points.front();
        if (normalized <= first.Position) {
            float alpha = std::clamp(first.Alpha * _transferSettings.OverallAlpha, 0.f, 1.f);
            return glm::vec4(first.Color, alpha);
        }

        auto const & last = points.back();
        if (normalized >= last.Position) {
            float alpha = std::clamp(last.Alpha * _transferSettings.OverallAlpha, 0.f, 1.f);
            return glm::vec4(last.Color, alpha);
        }

        for (std::size_t i = 1; i < points.size(); ++i) {
            auto const & lower = points[i - 1];
            auto const & upper = points[i];
            if (normalized <= upper.Position) {
                float span = upper.Position - lower.Position;
                float t = span > 0.f ? (normalized - lower.Position) / span : 0.f;
                glm::vec3 color = glm::mix(lower.Color, upper.Color, t);
                float alpha = glm::mix(lower.Alpha, upper.Alpha, t);
                alpha = std::clamp(alpha * _transferSettings.OverallAlpha, 0.f, 1.f);
                return glm::vec4(color, alpha);
            }
        }

        float alpha = std::clamp(last.Alpha * _transferSettings.OverallAlpha, 0.f, 1.f);
        return glm::vec4(last.Color, alpha);
    }

    void App::UpdateTransferFunctionTexture() {
        VCX::Engine::Texture2D<VCX::Engine::Formats::RGBA8> lut(kTransferLutSize, 1);
        for (std::size_t i = 0; i < kTransferLutSize; ++i) {
            float sample = static_cast<float>(i) / static_cast<float>(kTransferLutSize - 1);
            lut.At(i, 0) = EvaluateTransferFunction(sample);
        }
        _transferLutTexture.UpdateSampler({
            VCX::Engine::GL::WrapMode::Clamp,
            VCX::Engine::GL::WrapMode::Clamp,
            VCX::Engine::GL::WrapMode::Clamp,
            VCX::Engine::GL::FilterMode::Linear,
            VCX::Engine::GL::FilterMode::Linear,
        });
        _transferLutTexture.Update(lut);
        _transferDirty = false;
    }

    void App::UpdateAudioAnalysis(float deltaTime) {
        auto & settings = _analysisSettings;
        auto & state = _analysisState;
        settings.FftSizeIndex = ClampFftIndex(settings.FftSizeIndex);
        _fftSize = CurrentFftSize(settings);
        std::size_t fftSize = static_cast<std::size_t>(_fftSize);
        settings.NumBands = std::clamp(settings.NumBands, 1, 256);

        static bool sLoggedInit = false;
        if (!sLoggedInit) {
            spdlog::info("AudioAnalysis init fftSize {}, bands {}", _fftSize, settings.NumBands);
            sLoggedInit = true;
        }

        if (_fftCfg == nullptr || state.CachedWindowSize != _fftSize) {
            if (_fftCfg) {
                kiss_fft_free(_fftCfg);
            }
            _fftCfg = kiss_fft_alloc(_fftSize, 0, nullptr, nullptr);
            state.CachedWindowSize = _fftSize;
            spdlog::info("Rebuild FFT cfg size {} (cfg null? {})", _fftSize, _fftCfg == nullptr);
        }

        if (state.Window.size() != fftSize) {
            state.Window.assign(fftSize, 0.f);
        }
        if (state.Spectrum.size() != fftSize / 2) {
            state.Spectrum.assign(fftSize / 2, 0.f);
        }
        if (state.BandEnergies.size() != static_cast<std::size_t>(settings.NumBands)) {
            state.BandEnergies.assign(static_cast<std::size_t>(settings.NumBands), 0.f);
        }
        if (state.FftIn.size() != fftSize) {
            state.FftIn.resize(fftSize);
        }
        if (state.FftOut.size() != fftSize) {
            state.FftOut.resize(fftSize);
        }
        if (state.WindowCoeffs.size() != fftSize || state.CachedWindow != settings.Window) {
            BuildWindowCoeffs(state.WindowCoeffs, _fftSize, settings.Window);
            state.CachedWindow = settings.Window;
            spdlog::debug("Window coeffs built size {} type {}", _fftSize, static_cast<int>(settings.Window));
        }

        int headroom = std::clamp(_audioHeadroom, 0, _fftSize * 2);
        _audioHeadroom = headroom;
        auto readable = _audio.GetAvailableSamples();
        _audioReadable = readable;

        std::size_t read = _audio.GetLatestWindow(state.Window.data(), fftSize, static_cast<std::size_t>(headroom));
        if (read < fftSize) {
            ++state.Underruns;
        } else {
            ++_fftUpdateCounter;
        }

        float mean = 0.f;
        if (!state.Window.empty()) {
            mean = std::accumulate(state.Window.begin(), state.Window.end(), 0.f) / static_cast<float>(state.Window.size());
        }
        float sumSquares = 0.f;
        for (auto & sample : state.Window) {
            sample -= mean;
            sumSquares += sample * sample;
        }
        _audioWindowRms = state.Window.empty() ? 0.f : std::sqrt(sumSquares / static_cast<float>(state.Window.size()));

        ApplyWindow(state.WindowCoeffs, state.Window, _fftSize);

        auto const fftStart = std::chrono::high_resolution_clock::now();
        if (_fftCfg) {
            for (std::size_t i = 0; i < fftSize; ++i) {
                state.FftIn[i].r = state.Window[i];
                state.FftIn[i].i = 0.f;
            }
            kiss_fft(_fftCfg, state.FftIn.data(), state.FftOut.data());
            for (std::size_t i = 0; i < state.Spectrum.size(); ++i) {
                float re = state.FftOut[i].r;
                float im = state.FftOut[i].i;
                state.Spectrum[i] = std::sqrt(re * re + im * im) / static_cast<float>(_fftSize);
            }
        } else {
            std::fill(state.Spectrum.begin(), state.Spectrum.end(), 0.f);
        }
        auto const fftEnd = std::chrono::high_resolution_clock::now();
        state.LastFftMs = std::chrono::duration<float, std::milli>(fftEnd - fftStart).count();

        for (int b = 0; b < settings.NumBands; ++b) {
            BandRange range = ComputeBandRange(settings, b, settings.NumBands, _fftSize, static_cast<int>(_audio.GetSampleRate()));
            float energy = AggregateBand(state.Spectrum, range, settings.Aggregate);
            energy = ApplyCompression(energy, settings.CompressK);
            state.BandEnergies[static_cast<std::size_t>(b)] = energy;
        }

        float maxEnergy = 0.f;
        float minEnergy = std::numeric_limits<float>::max();
        float sumEnergy = 0.f;
        for (auto v : state.BandEnergies) {
            maxEnergy = std::max(maxEnergy, v);
            minEnergy = std::min(minEnergy, v);
            sumEnergy += v;
        }
        if (state.BandEnergies.empty()) {
            minEnergy = 0.f;
        }
        state.EnergyMin = minEnergy;
        state.EnergyMax = maxEnergy;
        state.EnergyAvg = state.BandEnergies.empty() ? 0.f : sumEnergy / static_cast<float>(state.BandEnergies.size());

        float gain = 1.f;
        if (settings.AgcEnabled) {
            gain = UpdateAgcGain(state.AgcGain, state.EnergyMax, settings, deltaTime);
            state.AgcGain = gain;
        } else {
            state.AgcGain = 1.f;
            gain = (state.EnergyMax > 1e-6f) ? 1.f / state.EnergyMax : 1.f;
        }
        for (auto & v : state.BandEnergies) {
            v = std::clamp(v * gain, 0.f, 1.f);
        }

        float normMin = 1.f;
        float normMax = 0.f;
        float normSum = 0.f;
        for (auto v : state.BandEnergies) {
            normMin = std::min(normMin, v);
            normMax = std::max(normMax, v);
            normSum += v;
        }
        if (state.BandEnergies.empty()) {
            normMin = 0.f;
        }
        state.EnergyMin = normMin;
        state.EnergyMax = normMax;
        state.EnergyAvg = state.BandEnergies.empty() ? 0.f : normSum / static_cast<float>(state.BandEnergies.size());

        int bassBands = std::min<int>(static_cast<int>(state.BandEnergies.size()), 3);
        float bassSum = 0.f;
        for (int i = 0; i < bassBands; ++i) {
            bassSum += state.BandEnergies[static_cast<std::size_t>(i)];
        }
        _audioBass = bassBands > 0 ? bassSum / static_cast<float>(bassBands) : 0.f;

        if (!state.Spectrum.empty()) {
            DownsampleSpectrum(state.Spectrum, state.SpectrumDownsample, 128);
        } else {
            state.SpectrumDownsample.clear();
        }

        auto const volumeStats = _volumeData.UpdateVolume(state.BandEnergies);
        _volumeBuildMs = volumeStats.BuildMs;
        _volumeUploadMs = volumeStats.UploadMs;
        _volumeLogTimer += deltaTime;
        if (_volumeLogTimer >= 1.f) {
            _volumeLogTimer -= 1.f;
            spdlog::info("Volume build {:.2f} ms, upload {:.2f} ms, energies min {:.4f}, max {:.4f}, avg {:.4f}",
                _volumeBuildMs,
                _volumeUploadMs,
                state.EnergyMin,
                state.EnergyMax,
                state.EnergyAvg);
        }

        if (!state.Window.empty()) {
            float step = static_cast<float>(state.Window.size()) / static_cast<float>(_oscilloscopePoints.size());
            for (std::size_t i = 0; i < _oscilloscopePoints.size(); ++i) {
                std::size_t idx = std::min(state.Window.size() - 1, static_cast<std::size_t>(i * step));
                _oscilloscopePoints[i] = state.Window[idx];
            }
        } else {
            _oscilloscopePoints.fill(0.f);
        }

        _audioLogTimer += deltaTime;
        if (_audioLogTimer >= 1.f) {
            _audioLogTimer -= 1.f;
            _fftUpdatesPerSecond = _fftUpdateCounter;
            _fftUpdateCounter = 0;
            float fill = _audio.GetRingFillRatio();
            spdlog::info("Audio stats fill {:.3f}, readable {}, fftUpdates {}, windowRMS {:.5f}, overrun {}, dropped {}, underrun {}, headroom {}",
                fill,
                readable,
                _fftUpdatesPerSecond,
                _audioWindowRms,
                _audio.GetOverrunWrites(),
                _audio.GetDroppedSamples(),
                _audio.GetUnderrunReads(),
                headroom);
        }

        _fftLogTimer += deltaTime;
        if (_fftLogTimer >= 1.f) {
            _fftLogTimer -= 1.f;
            spdlog::info("FFT {:.2f} ms, energy min {:.4f}, max {:.4f}, avg {:.4f}, agc {:.3f}, underruns {}", state.LastFftMs, state.EnergyMin, state.EnergyMax, state.EnergyAvg, state.AgcGain, state.Underruns);
            state.Underruns = 0;
        }
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
        uniforms.SetByName("uBass", _audioBass);
        uniforms.SetByName("uShellMode", static_cast<int>(_dynamicSettings.Mode));

        if (_statsBuffer) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, _statsBuffer);
        }
        if (_transferDirty) {
            UpdateTransferFunctionTexture();
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, _transferLutTexture.Get());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, volumeTex);

        {
            auto const progUse = _volumeProgram.Use();
            auto const vaoUse = _fullscreenVAO.Use();
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
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
        UpdateAudioAnalysis(deltaTime);
        RenderVolume(deltaTime);

        ImGui::Begin("Sphere Audio Visualizer");
        ImGui::Text("FPS: %.1f", CurrentFps());
        if (ImGui::Button("Reload Config")) {
            LoadConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Config")) {
            SaveConfig();
        }
        ImGui::SliderFloat("alpha", &_alpha, 0.f, 1.f);
        ImGui::Separator();

        RenderAudioUI();

        if (ImGui::CollapsingHeader("Volume Shell", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto settings = _volumeData.GetSettings();
            bool settingsChanged = false;
            int volumeSizeInput = static_cast<int>(settings.VolumeSize);
            if (ImGui::InputInt("Volume Size", &volumeSizeInput)) {
                settings.VolumeSize = static_cast<std::size_t>(std::clamp(volumeSizeInput, 32, 256));
                settingsChanged = true;
            }

            if (ImGui::SliderFloat("Amplitude Scale", &settings.AmpScale, 0.f, 5.f)) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Thickness Scale", &settings.ThicknessScale, 0.f, 5.f)) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Base Thickness", &settings.BaseThickness, 0.01f, 0.5f)) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Global Gain", &settings.GlobalGain, 0.1f, 5.f)) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Smoothing Factor", &settings.SmoothingFactor, 0.f, 1.f)) {
                settingsChanged = true;
            }
            if (ImGui::SliderFloat("Tilt (low->high)", &settings.Tilt, -1.f, 1.f)) {
                settingsChanged = true;
            }

            const char * layoutNames[] = { "Linear", "Log" };
            int layoutIndex = static_cast<int>(settings.RadiusLayout);
            if (ImGui::Combo("Radius Layout", &layoutIndex, layoutNames, IM_ARRAYSIZE(layoutNames))) {
                settings.RadiusLayout = static_cast<SphereVolumeData::RadiusDistribution>(layoutIndex);
                settingsChanged = true;
            }

            ImGui::Text("Volume build: %.2f ms, upload: %.2f ms", _volumeBuildMs, _volumeUploadMs);
            if (ImGui::Button("Regenerate")) {
                settingsChanged = true;
            }

            if (settingsChanged) {
                _volumeData.SetSettings(settings);
                _volumeData.Regenerate();
            }
        }

        ImGui::Separator();
        ImGui::Text("Raymarch");
        ImGui::SliderFloat("Step Size", &_renderSettings.StepSize, 0.001f, 0.1f);
        ImGui::SliderInt("Max Steps", &_renderSettings.MaxSteps, 16, 512);
        ImGui::SliderFloat("Alpha Scale", &_renderSettings.AlphaScale, 0.1f, 10.f);
        ImGui::Checkbox("Enable Jitter", &_renderSettings.EnableJitter);
        int mode = static_cast<int>(_renderSettings.Mode);
        const char * colorModes[] = { "Grayscale", "Transfer LUT" };
        if (ImGui::Combo("Color Mode", &mode, colorModes, IM_ARRAYSIZE(colorModes))) {
            _renderSettings.Mode = static_cast<ColorMode>(mode);
        }

        ImGui::Separator();
        ImGui::Text("Dynamics");
        ImGui::Text("Bass: %.3f", _audioBass);
        const char * perturbModes[] = { "Ripple", "Noise" };
        int modeIndex = static_cast<int>(_dynamicSettings.Mode);
        if (ImGui::Combo("Perturb Mode", &modeIndex, perturbModes, IM_ARRAYSIZE(perturbModes))) {
            _dynamicSettings.Mode = static_cast<App::PerturbMode>(modeIndex);
            LogDynamicParam("perturbMode", static_cast<float>(modeIndex));
        }
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

        RenderTransferFunctionUI();

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
