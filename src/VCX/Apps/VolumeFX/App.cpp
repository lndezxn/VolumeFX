#include "Apps/VolumeFX/App.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
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
            Engine::GL::SharedShader("assets/shaders/volume_fx.frag") })),
        _cube(
            Engine::GL::VertexLayout()
                .Add<Vertex>("vertex", Engine::GL::DrawFrequency::Static)
                .At(0, &Vertex::Position)
                .At(1, &Vertex::Color)) {
        _cube.UpdateVertexBuffer("vertex", Engine::make_span_bytes<Vertex>(std::span(c_CubeVertices)));
        _cube.UpdateElementBuffer(c_CubeIndices);
        glEnable(GL_DEPTH_TEST);
    }

    void App::OnFrame() {
        updateCamera();

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
        glm::mat4 const view = glm::lookAt(cameraPosition(), _cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        float const spin = static_cast<float>(glfwGetTime() * 0.25);
        glm::mat4 const model = glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0.0f, 1.0f, 0.0f));

        auto & uniforms = _program.GetUniforms();
        uniforms.SetByName("u_Model", model);
        uniforms.SetByName("u_ViewProj", projection * view);

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
        ImGui::End();
    }
} // namespace VCX::Apps::VolumeFX
