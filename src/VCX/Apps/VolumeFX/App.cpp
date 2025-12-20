#include "Apps/VolumeFX/App.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#undef APIENTRY

#pragma comment(lib, "winmm.lib")
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/trigonometric.hpp>

#include "Engine/prelude.hpp"

namespace {
    using VCX::Apps::VolumeFX::App;
    using Vertex = App::Vertex;

    constexpr std::array<Vertex, 8> c_CubeVertices { {
        { .Position { -0.8f, -0.8f,  0.8f }, .Color { 0.75f, 0.36f, 0.95f } },
        { .Position {  0.8f, -0.8f,  0.8f }, .Color { 0.36f, 0.82f, 0.98f } },
        { .Position {  0.8f,  0.8f,  0.8f }, .Color { 0.25f, 0.95f, 0.62f } },
        { .Position { -0.8f,  0.8f,  0.8f }, .Color { 0.98f, 0.78f, 0.36f } },
        { .Position { -0.8f, -0.8f, -0.8f }, .Color { 0.72f, 0.32f, 0.95f } },
        { .Position {  0.8f, -0.8f, -0.8f }, .Color { 0.32f, 0.76f, 0.95f } },
        { .Position {  0.8f,  0.8f, -0.8f }, .Color { 0.28f, 0.95f, 0.76f } },
        { .Position { -0.8f,  0.8f, -0.8f }, .Color { 0.95f, 0.62f, 0.28f } },
    } };

    constexpr std::array<std::uint32_t, 36> c_CubeIndices { {
        0, 1, 2, 2, 3, 0,
        1, 5, 6, 6, 2, 1,
        5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4,
        3, 2, 6, 6, 7, 3,
        4, 5, 1, 1, 0, 4
    } };
}

namespace VCX::Apps::VolumeFX {
    App::App() :
        _program(Engine::GL::UniqueProgram({
            Engine::GL::SharedShader("assets/shaders/volume_fx.vert"),
            Engine::GL::SharedShader("assets/shaders/volume_raymarch.frag") })),
        _decayProgram(Engine::GL::UniqueProgram({
            Engine::GL::SharedShader("assets/shaders/density_decay.comp") })),
        _injectProgram(Engine::GL::UniqueProgram({
            Engine::GL::SharedShader("assets/shaders/density_inject.comp") })),
        _cube(
            Engine::GL::VertexLayout()
                .Add<Vertex>("vertex", Engine::GL::DrawFrequency::Static)
                .At(0, &Vertex::Position)
                .At(1, &Vertex::Color)) {
        _cube.UpdateVertexBuffer("vertex", Engine::make_span_bytes<Vertex>(std::span(c_CubeVertices)));
        _cube.UpdateElementBuffer(c_CubeIndices);
        initDensityTextures();
        _baseGain = _visualizationGain;
        _program.GetUniforms().SetByName("u_DensityTex", 0);
        _decayProgram.GetUniforms().SetByName("u_In", 1);
        glEnable(GL_DEPTH_TEST);
    }

    App::~App() {
        stopAudioPlayback();
        for (auto & tex : _densityTex) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
    }

    void App::initDensityTextures() {
        for (auto & tex : _densityTex) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }

        std::vector<float> density(static_cast<std::size_t>(_gridSize.x) * _gridSize.y * _gridSize.z, 0.0f);
        glm::vec3 const gridSizeF = glm::vec3(_gridSize);
        glm::vec3 const denom = glm::max(gridSizeF - glm::vec3(1.0f), glm::vec3(1.0f));
        glm::vec3 const center(0.5f);
        float const radius = 0.35f;
        float const falloff = 0.12f;

        std::size_t idx = 0;
        for (int z = 0; z < _gridSize.z; ++z) {
            for (int y = 0; y < _gridSize.y; ++y) {
                for (int x = 0; x < _gridSize.x; ++x) {
                    glm::vec3 const pos = glm::vec3(x, y, z) / denom;
                    float const dist = glm::length(pos - center);
                    float const value = glm::clamp(1.0f - glm::smoothstep(radius - falloff, radius, dist), 0.0f, 1.0f);
                    density[idx++] = value;
                }
            }
        }

        glGenTextures(static_cast<GLsizei>(_densityTex.size()), _densityTex.data());

        auto const configureTexture = [&](GLuint tex) {
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexImage3D(
                GL_TEXTURE_3D,
                0,
                GL_R16F,
                _gridSize.x,
                _gridSize.y,
                _gridSize.z,
                0,
                GL_RED,
                GL_FLOAT,
                nullptr);
            glTexSubImage3D(
                GL_TEXTURE_3D,
                0,
                0,
                0,
                0,
                _gridSize.x,
                _gridSize.y,
                _gridSize.z,
                GL_RED,
                GL_FLOAT,
                density.data());
        };

        for (auto const tex : _densityTex) {
            configureTexture(tex);
        }

        glBindTexture(GL_TEXTURE_3D, 0);
        _densitySrc = 0;
    }

    GLuint App::densityReadTexture() const {
        return _densityTex[_densitySrc];
    }

    GLuint App::densityWriteTexture() const {
        return _densityTex[1 - _densitySrc];
    }

    void App::decayDensityField() {
        if (_densityTex[0] == 0 || _densityTex[1] == 0) {
            return;
        }

        auto & uniforms = _decayProgram.GetUniforms();
        uniforms.SetByName("u_Size", _gridSize);
        uniforms.SetByName("u_Dissipation", _dissipation);
        auto const useProgram = _decayProgram.Use();

        glBindImageTexture(0, densityWriteTexture(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, densityReadTexture());

        auto const divUp = [](int value) {
            return (value + 7) / 8;
        };
        GLuint const groupsX = static_cast<GLuint>(divUp(_gridSize.x));
        GLuint const groupsY = static_cast<GLuint>(divUp(_gridSize.y));
        GLuint const groupsZ = static_cast<GLuint>(divUp(_gridSize.z));
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE0);
        _densitySrc = 1 - _densitySrc;
    }

    void App::injectDensityField() {
        if (_densityTex[0] == 0) {
            return;
        }

        auto const useProgram = _injectProgram.Use();
        auto & uniforms = _injectProgram.GetUniforms();
        uniforms.SetByName("u_Size", _gridSize);
        float const audioLevel = glm::clamp(_currentAudioLevel, 0.0f, 1.5f);
        float const levelBoost = _autoGainEnabled ? glm::clamp(0.5f + 1.5f * audioLevel, 0.5f, 2.5f) : 1.0f;
        float const frameScale = glm::clamp(Engine::GetDeltaTime() * 60.0f, 0.25f, 3.0f);
        uniforms.SetByName("u_EmitStrength", _emitStrength * _visualizationGain * levelBoost * frameScale);
        uniforms.SetByName("u_Sigma", _emitSigma);
        uniforms.SetByName("u_EmitterRadius", _emitterRadius);
        uniforms.SetByName("u_Time", _mockPlaybackTime);
        uniforms.SetByName("u_Emitters", std::min(_emitterCount, 4));

        glBindImageTexture(0, densityReadTexture(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R16F);

        GLuint const groupsX = static_cast<GLuint>((_gridSize.x + 7) / 8);
        GLuint const groupsY = static_cast<GLuint>((_gridSize.y + 7) / 8);
        GLuint const groupsZ = static_cast<GLuint>((_gridSize.z + 7) / 8);
        glDispatchCompute(groupsX, groupsY, groupsZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void App::OnFrame() {
        updateCamera();
        updateAudioReactivity();
        injectDensityField();
        decayDensityField();

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderScene();
        renderUI();
    }

    void App::updateCamera() {
        auto & io = ImGui::GetIO();

        if (! io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            _isOrbiting = true;
            _orbitAngles.x += io.MouseDelta.x * 0.005f;
            _orbitAngles.y += io.MouseDelta.y * 0.005f;
            _orbitAngles.y = std::clamp(_orbitAngles.y, -1.2f, 1.2f);
        }

        if (! ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            _isOrbiting = false;
        }

        if (! io.WantCaptureMouse && io.MouseWheel != 0.0f) {
            _cameraDistance = std::clamp(_cameraDistance - io.MouseWheel * 0.4f, 1.2f, 14.0f);
        }

        if (_autoRotate && ! _isOrbiting) {
            _orbitAngles.x += Engine::GetDeltaTime() * 0.15f;
        }
    }

    void App::updateAudioReactivity() {
        _baseGain = std::clamp(_baseGain, 0.1f, 4.0f);

        float const deltaTime = Engine::GetDeltaTime();
        _mockPlaybackTime += deltaTime;
        if (_audioLoaded && _audioDuration > 0.0f) {
            if (_audioLoopPlayback) {
                _mockPlaybackTime = std::fmod(_mockPlaybackTime, _audioDuration);
            } else {
                _mockPlaybackTime = std::min(_mockPlaybackTime, _audioDuration);
            }
        }

        bool const haveAudio = _audioLoaded && !_audioEnvelope.empty() && _audioSampleRate > 0.0f;
        float modulation = 0.0f;
        if (haveAudio) {
            modulation = sampleAudioEnvelope(_mockPlaybackTime);
        } else {
            float const speed = glm::max(_autoGainSpeed, 0.0f);
            _autoGainPhase += deltaTime * glm::max(0.1f, speed);
            float const twoPi = glm::pi<float>() * 2.0f;
            if (_autoGainPhase > twoPi) {
                _autoGainPhase = std::fmod(_autoGainPhase, twoPi);
            }
            modulation = 0.5f * (std::sin(_autoGainPhase) + 1.0f);
        }
        _currentAudioLevel = modulation;

        if (! _autoGainEnabled) {
            _visualizationGain = _baseGain;
            return;
        }

        float const depth = glm::clamp(_autoGainDepth, 0.0f, 2.0f);
        float const scale = 1.0f + depth * modulation;
        _visualizationGain = glm::clamp(_baseGain * scale, 0.05f, 8.0f);
    }

    void App::stopAudioPlayback() {
        if (_audioPlaying) {
            PlaySoundW(nullptr, nullptr, 0);
        }
        _audioPlaying = false;
    }

    void App::startAudioPlayback(const std::wstring & path, bool loop) {
        stopAudioPlayback();
        if (path.empty()) {
            return;
        }
        UINT flags = SND_FILENAME | SND_ASYNC;
        if (loop) {
            flags |= SND_LOOP;
        }
        if (! PlaySoundW(path.c_str(), nullptr, flags)) {
            _audioStatus = "Failed to play audio.";
            _audioPlaying = false;
        } else {
            _audioPlaying = true;
        }
    }

    float App::sampleAudioEnvelope(float t) const {
        if (_audioEnvelope.empty() || _audioSampleRate <= 0.0f) {
            return 0.0f;
        }

        float const duration = (_audioDuration > 0.0f) ? _audioDuration : static_cast<float>(_audioEnvelope.size()) / _audioSampleRate;
        if (duration <= 0.0f) {
            return 0.0f;
        }

        float const minTime = 0.0f;
        float const maxTime = std::max(minTime, duration - (1.0f / _audioSampleRate));
        float const clamped = std::clamp(t, minTime, maxTime);
        float const samplePos = clamped * _audioSampleRate;
        std::size_t const i0 = static_cast<std::size_t>(samplePos);
        if (i0 >= _audioEnvelope.size() - 1) {
            return _audioEnvelope.back();
        }

        float const frac = samplePos - static_cast<float>(i0);
        return std::lerp(_audioEnvelope[i0], _audioEnvelope[i0 + 1], frac);
    }

    bool App::loadAudioFile(const char * path) {
        if (path == nullptr || path[0] == '\0') {
            _audioStatus = "Please provide a file path.";
            return false;
        }

        stopAudioPlayback();
        _audioLoaded = false;
        _audioEnvelope.clear();
        _audioSampleRate = 0.0f;
        _audioDuration = 0.0f;
        _currentAudioLevel = 0.0f;
        _mockPlaybackTime = 0.0f;

        namespace fs = std::filesystem;
        fs::path resolved = fs::u8path(path);
        if (resolved.is_relative()) {
            std::error_code ec;
            resolved = fs::current_path(ec) / resolved;
        }

        std::ifstream file(resolved, std::ios::binary);
        if (! file) {
            _audioStatus = "Unable to open audio file.";
            return false;
        }

        auto readBytes = [&](void * dst, std::size_t size) {
            return static_cast<bool>(file.read(static_cast<char *>(dst), static_cast<std::streamsize>(size)));
        };

        char riff[4];
        if (! readBytes(riff, 4) || std::strncmp(riff, "RIFF", 4) != 0) {
            _audioStatus = "Invalid WAV: missing RIFF header.";
            return false;
        }

        [[maybe_unused]] std::uint32_t riffSize = 0;
        if (! readBytes(&riffSize, 4)) {
            _audioStatus = "Invalid WAV: truncated size.";
            return false;
        }

        char wave[4];
        if (! readBytes(wave, 4) || std::strncmp(wave, "WAVE", 4) != 0) {
            _audioStatus = "Invalid WAV: missing WAVE tag.";
            return false;
        }

        struct ChunkHeader {
            char          id[4];
            std::uint32_t size;
        };

        struct FmtChunkData {
            std::uint16_t audioFormat = 0;
            std::uint16_t numChannels = 0;
            std::uint32_t sampleRate = 0;
            std::uint32_t byteRate = 0;
            std::uint16_t blockAlign = 0;
            std::uint16_t bitsPerSample = 0;
        };

        FmtChunkData fmt { };
        bool fmtFound = false;
        bool dataFound = false;
        std::vector<char> audioData;

        auto skipPadding = [&](std::uint32_t size) {
            if ((size & 1u) != 0u) {
                file.seekg(1, std::ios::cur);
            }
        };

        ChunkHeader chunk { };
        while (readBytes(&chunk, sizeof(chunk))) {
            std::string chunkId(chunk.id, 4);
            if (chunkId == "fmt ") {
                std::vector<char> buffer(chunk.size);
                if (! buffer.empty() && ! readBytes(buffer.data(), buffer.size())) {
                    _audioStatus = "Invalid WAV: truncated fmt chunk.";
                    return false;
                }
                if (chunk.size < 16) {
                    _audioStatus = "Invalid WAV: fmt chunk too small.";
                    return false;
                }
                std::size_t const bytesToCopy = std::min<std::size_t>(sizeof(FmtChunkData), buffer.size());
                std::memcpy(&fmt, buffer.data(), bytesToCopy);
                fmtFound = true;
            } else if (chunkId == "data") {
                audioData.resize(chunk.size);
                if (! audioData.empty() && ! readBytes(audioData.data(), audioData.size())) {
                    _audioStatus = "Invalid WAV: truncated data chunk.";
                    return false;
                }
                dataFound = true;
                break;
            } else {
                file.seekg(static_cast<std::streamoff>(chunk.size), std::ios::cur);
            }

            skipPadding(chunk.size);
        }

        if (! fmtFound || ! dataFound) {
            _audioStatus = "Invalid WAV: missing fmt or data chunk.";
            return false;
        }

        if (fmt.audioFormat != 1 || fmt.bitsPerSample != 16) {
            _audioStatus = "Only PCM16 WAV files are supported.";
            return false;
        }

        if (fmt.numChannels == 0 || fmt.blockAlign == 0 || fmt.sampleRate == 0) {
            _audioStatus = "Invalid WAV: unsupported channel configuration.";
            return false;
        }

        std::size_t const bytesPerSample = fmt.bitsPerSample / 8;
        if (bytesPerSample == 0) {
            _audioStatus = "Invalid WAV: zero bytes per sample.";
            return false;
        }

        std::size_t const totalSamples = audioData.size() / bytesPerSample;
        if (totalSamples == 0) {
            _audioStatus = "Audio file contains no samples.";
            return false;
        }

        std::size_t const frameCount = totalSamples / fmt.numChannels;
        std::vector<float> envelope(frameCount);
        auto const * pcm = reinterpret_cast<std::int16_t const *>(audioData.data());
        double const norm = 1.0 / (32768.0 * static_cast<double>(fmt.numChannels));
        float env = 0.0f;
        float const attack = 0.35f;
        float const release = 0.08f;

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            double accum = 0.0;
            for (std::uint16_t ch = 0; ch < fmt.numChannels; ++ch) {
                std::size_t idx = frame * fmt.numChannels + ch;
                if (idx >= totalSamples) {
                    break;
                }
                accum += static_cast<double>(pcm[idx]);
            }
            float mono = static_cast<float>(accum * norm);
            float magnitude = std::abs(mono);
            float const coeff = (magnitude > env) ? attack : release;
            env = std::lerp(env, magnitude, coeff);
            envelope[frame] = env;
        }

        _audioEnvelope = std::move(envelope);
        _audioSampleRate = static_cast<float>(fmt.sampleRate);
        _audioDuration = (_audioSampleRate > 0.0f) ? static_cast<float>(_audioEnvelope.size()) / _audioSampleRate : 0.0f;
        _audioLoaded = true;
        _currentAudioLevel = 0.0f;
        _mockPlaybackTime = 0.0f;
        _audioPathW = resolved.wstring();

        startAudioPlayback(_audioPathW, _audioLoopPlayback);

        std::ostringstream oss;
        oss << "Loaded WAV (" << fmt.numChannels << " ch, " << fmt.sampleRate << " Hz, "
            << std::fixed << std::setprecision(2) << _audioDuration << " s)";
        _audioStatus = oss.str();
        return true;
    }

    glm::vec3 App::cameraPosition() const {
        float const yaw   = _orbitAngles.x;
        float const pitch = _orbitAngles.y;
        float const x = _cameraDistance * glm::cos(pitch) * glm::sin(yaw);
        float const y = _cameraDistance * glm::sin(pitch);
        float const z = _cameraDistance * glm::cos(pitch) * glm::cos(yaw);
        return _cameraTarget + glm::vec3(x, y, z);
    }

    void App::renderScene() {
        auto const [frameW, frameH] = Engine::GetCurrentFrameSize();
        float const aspect = frameH == 0 ? 1.0f : static_cast<float>(frameW) / static_cast<float>(frameH);

        glm::mat4 const projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
        glm::vec3 const camPos = cameraPosition();
        glm::mat4 const view = glm::lookAt(camPos, _cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        float const spin = static_cast<float>(glfwGetTime() * 0.25);
        glm::mat4 const model = glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 const modelInv = glm::inverse(model);
        glm::vec3 const boxMin(-0.8f);
        glm::vec3 const boxMax(0.8f);

        auto & uniforms = _program.GetUniforms();
        uniforms.SetByName("u_Model", model);
        uniforms.SetByName("u_ModelInv", modelInv);
        uniforms.SetByName("u_ViewProj", projection * view);
        uniforms.SetByName("u_CameraPos", camPos);
        uniforms.SetByName("u_StepCount", static_cast<std::int32_t>(_raymarchSteps));
        uniforms.SetByName("u_DensityScale", _visualizationGain);
        uniforms.SetByName("u_Thresh", _densityThreshold);
        uniforms.SetByName("u_BoxMin", boxMin);
        uniforms.SetByName("u_BoxMax", boxMax);

        if (densityReadTexture() != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, densityReadTexture());
        }
        _cube.Draw({ _program.Use() });
    }

    void App::renderUI() {
        ImGui::SetNextWindowSizeConstraints({ 360, 0 }, { 600, FLT_MAX });
        ImGui::Begin("Audio Input");
        ImGui::TextWrapped("Stream any audio source to drive volume rendering parameters later.");
        ImGui::InputText("Source file", _audioPathBuffer.data(), _audioPathBuffer.size());
        if (ImGui::Button("Load audio")) {
            if (_audioPathBuffer[0] != '\0') {
                loadAudioFile(_audioPathBuffer.data());
            } else {
                _audioStatus = "Please provide a file path.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            _audioPathBuffer.fill('\0');
            _audioStatus = "No audio loaded.";
            _audioEnvelope.clear();
            _audioLoaded = false;
            _audioSampleRate = 0.0f;
            _audioDuration = 0.0f;
            _currentAudioLevel = 0.0f;
            _mockPlaybackTime = 0.0f;
            stopAudioPlayback();
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-seed volume")) {
            initDensityTextures();
        }
        bool loopBefore = _audioLoopPlayback;
        ImGui::Checkbox("Loop playback", &_audioLoopPlayback);
        if (_audioLoopPlayback != loopBefore && _audioPlaying && ! _audioPathW.empty()) {
            startAudioPlayback(_audioPathW, _audioLoopPlayback);
        }
        float const playbackRatio = (_audioDuration > 0.0f) ? std::clamp(_mockPlaybackTime / _audioDuration, 0.0f, 1.0f) : 0.0f;
        ImGui::ProgressBar(playbackRatio, ImVec2(-1.0f, 0.0f));
        ImGui::Text("Playback: %.2f / %.2f s", _mockPlaybackTime, _audioDuration);
        ImGui::Text("Audio level: %.2f", _currentAudioLevel);

        ImGui::Separator();
        ImGui::SliderFloat("Base gain", &_baseGain, 0.1f, 4.0f, "%.2f");
        ImGui::Checkbox("React to audio", &_autoGainEnabled);
        ImGui::BeginDisabled(! _autoGainEnabled);
        ImGui::SliderFloat("Mod depth", &_autoGainDepth, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Fallback speed", &_autoGainSpeed, 0.1f, 5.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::Text("Live gain: %.2f", _visualizationGain);
        ImGui::TextWrapped("Status: %s", _audioStatus.c_str());
        ImGui::End();

        ImGui::SetNextWindowSizeConstraints({ 260, 0 }, { 480, FLT_MAX });
        ImGui::Begin("Scene Controls");
        ImGui::Checkbox("Auto rotate", &_autoRotate);
        ImGui::SliderFloat("Camera distance", &_cameraDistance, 1.2f, 14.0f);
        ImGui::TextUnformatted("Right-drag to orbit, scroll to zoom.");
        ImGui::Text("Gain feeds renderer scale: %.2f", _visualizationGain);
        ImGui::SliderFloat("Density thresh", &_densityThreshold, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("Dissipation", &_dissipation, 0.5f, 1.0f, "%.4f");
        ImGui::SliderFloat("Emit strength", &_emitStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Emit sigma", &_emitSigma, 0.01f, 0.3f, "%.3f");
        ImGui::SliderFloat("Emitter radius", &_emitterRadius, 0.0f, 0.45f, "%.3f");
        ImGui::SliderInt("Emitter count", &_emitterCount, 1, 4);
        ImGui::End();
    }
} // namespace VCX::Apps::VolumeFX
