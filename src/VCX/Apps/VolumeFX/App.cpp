#include "Apps/VolumeFX/App.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
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
        _cube(
            Engine::GL::VertexLayout()
                .Add<Vertex>("vertex", Engine::GL::DrawFrequency::Static)
                .At(0, &Vertex::Position)
                .At(1, &Vertex::Color)) {
        _cube.UpdateVertexBuffer("vertex", Engine::make_span_bytes<Vertex>(std::span(c_CubeVertices)));
        _cube.UpdateElementBuffer(c_CubeIndices);
        initDensityTextures();
        _program.GetUniforms().SetByName("u_DensityTex", 0);
        _decayProgram.GetUniforms().SetByName("u_In", 1);
        glEnable(GL_DEPTH_TEST);
    }

    App::~App() {
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

    void App::OnFrame() {
        updateCamera();
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
        if (_audioStatus.rfind("Loaded", 0) == 0) {
            _mockPlaybackTime += Engine::GetDeltaTime();
        }

        ImGui::SetNextWindowSizeConstraints({ 360, 0 }, { 600, FLT_MAX });
        ImGui::Begin("Audio Input");
        ImGui::TextWrapped("Stream any audio source to drive volume rendering parameters later.");
        ImGui::InputText("Source file", _audioPathBuffer.data(), _audioPathBuffer.size());
        if (ImGui::Button("Load audio")) {
            if (_audioPathBuffer[0] != '\0') {
                _audioStatus = std::string("Loaded (stub): ") + _audioPathBuffer.data();
                _mockPlaybackTime = 0.0f;
            } else {
                _audioStatus = "Please provide a file path.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            _audioPathBuffer.fill('\0');
            _audioStatus = "No audio loaded.";
            _mockPlaybackTime = 0.0f;
        }

        ImGui::SliderFloat("Gain", &_visualizationGain, 0.1f, 4.0f, "%.2f");
        ImGui::TextWrapped("Status: %s", _audioStatus.c_str());
        ImGui::Text("Playback (mock): %.2f s", _mockPlaybackTime);
        ImGui::End();

        ImGui::SetNextWindowSizeConstraints({ 260, 0 }, { 480, FLT_MAX });
        ImGui::Begin("Scene Controls");
        ImGui::Checkbox("Auto rotate", &_autoRotate);
        ImGui::SliderFloat("Camera distance", &_cameraDistance, 1.2f, 14.0f);
        ImGui::TextUnformatted("Right-drag to orbit, scroll to zoom.");
        ImGui::Text("Gain feeds renderer scale (stub): %.2f", _visualizationGain);
        ImGui::SliderFloat("Density thresh", &_densityThreshold, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("Dissipation", &_dissipation, 0.5f, 1.0f, "%.4f");
        ImGui::End();
    }
} // namespace VCX::Apps::VolumeFX
