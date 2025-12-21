#include "Apps/VolumeFX/VolumeRenderer.h"

#include <array>
#include <cstdint>
#include <span>

#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "Apps/VolumeFX/OrbitCamera.h"
#include "Engine/app.h"
#include "Engine/prelude.hpp"

namespace {
    using VCX::Apps::VolumeFX::VolumeRenderer;
    using Vertex = VolumeRenderer::Vertex;

    constexpr float kBoxExtent = 1.2f;

    constexpr std::array<Vertex, 8> c_CubeVertices { {
        { .Position { -kBoxExtent, -kBoxExtent,  kBoxExtent }, .Color { 0.75f, 0.36f, 0.95f } },
        { .Position {  kBoxExtent, -kBoxExtent,  kBoxExtent }, .Color { 0.36f, 0.82f, 0.98f } },
        { .Position {  kBoxExtent,  kBoxExtent,  kBoxExtent }, .Color { 0.25f, 0.95f, 0.62f } },
        { .Position { -kBoxExtent,  kBoxExtent,  kBoxExtent }, .Color { 0.98f, 0.78f, 0.36f } },
        { .Position { -kBoxExtent, -kBoxExtent, -kBoxExtent }, .Color { 0.72f, 0.32f, 0.95f } },
        { .Position {  kBoxExtent, -kBoxExtent, -kBoxExtent }, .Color { 0.32f, 0.76f, 0.95f } },
        { .Position {  kBoxExtent,  kBoxExtent, -kBoxExtent }, .Color { 0.28f, 0.95f, 0.76f } },
        { .Position { -kBoxExtent,  kBoxExtent, -kBoxExtent }, .Color { 0.95f, 0.62f, 0.28f } },
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
    VolumeRenderer::VolumeRenderer() :
        _program(Engine::GL::UniqueProgram({
            Engine::GL::SharedShader("assets/shaders/volume_fx.vert"),
            Engine::GL::SharedShader("assets/shaders/volume_raymarch.frag") })),
        _boxProgram(Engine::GL::UniqueProgram({
            Engine::GL::SharedShader("assets/shaders/volume_fx.vert"),
            Engine::GL::SharedShader("assets/shaders/volume_box.frag") })),
        _cube(
            Engine::GL::VertexLayout()
                .Add<Vertex>("vertex", Engine::GL::DrawFrequency::Static)
                .At(0, &Vertex::Position)
                .At(1, &Vertex::Color)) {
        _cube.UpdateVertexBuffer("vertex", Engine::make_span_bytes<Vertex>(std::span(c_CubeVertices)));
        _cube.UpdateElementBuffer(c_CubeIndices);
        _program.GetUniforms().SetByName("u_DensityTex", 0);
        _boxProgram.GetUniforms().SetByName("u_DensityTex", 0);
    }

    void VolumeRenderer::Render(GLuint densityTex, const OrbitCamera & camera, float visualizationGain, float densityThreshold, bool showBoundingBox) {
        auto const [frameW, frameH] = Engine::GetCurrentFrameSize();
        float const aspect = frameH == 0 ? 1.0f : static_cast<float>(frameW) / static_cast<float>(frameH);

        glm::mat4 const projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
        glm::vec3 const camPos = camera.Position();
        glm::mat4 const view = glm::lookAt(camPos, camera.Target(), glm::vec3(0.0f, 1.0f, 0.0f));
        float const spin = static_cast<float>(glfwGetTime() * 0.25);
        glm::mat4 const model = glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 const modelInv = glm::inverse(model);
        glm::vec3 const boxMin(-kBoxExtent);
        glm::vec3 const boxMax(kBoxExtent);

        auto & uniforms = _program.GetUniforms();
        uniforms.SetByName("u_Model", model);
        uniforms.SetByName("u_ModelInv", modelInv);
        uniforms.SetByName("u_ViewProj", projection * view);
        uniforms.SetByName("u_CameraPos", camPos);
        uniforms.SetByName("u_StepCount", static_cast<std::int32_t>(_raymarchSteps));
        uniforms.SetByName("u_DensityScale", visualizationGain);
        uniforms.SetByName("u_Thresh", densityThreshold);
        uniforms.SetByName("u_BoxMin", boxMin);
        uniforms.SetByName("u_BoxMax", boxMax);

        if (densityTex != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, densityTex);
        }
        _cube.Draw({ _program.Use() });

        if (showBoundingBox) {
            auto & boxUniforms = _boxProgram.GetUniforms();
            boxUniforms.SetByName("u_Model", model);
            boxUniforms.SetByName("u_ViewProj", projection * view);
            boxUniforms.SetByName("u_Color", glm::vec3(0.1f, 0.8f, 0.9f));

            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            _cube.Draw({ _boxProgram.Use() });
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }

    int VolumeRenderer::RaymarchSteps() const {
        return _raymarchSteps;
    }

    void VolumeRenderer::SetRaymarchSteps(int steps) {
        _raymarchSteps = std::max(1, steps);
    }
} // namespace VCX::Apps::VolumeFX
