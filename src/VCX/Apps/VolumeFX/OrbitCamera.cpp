#include "Apps/VolumeFX/OrbitCamera.h"

#include <algorithm>

#include <imgui.h>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

namespace VCX::Apps::VolumeFX {
    OrbitCamera::OrbitCamera() = default;

    void OrbitCamera::Update(const ImGuiIO & io, float deltaTime) {
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
            _orbitAngles.x += deltaTime * 0.15f;
        }
    }

    glm::vec3 OrbitCamera::Position() const {
        float const yaw   = _orbitAngles.x;
        float const pitch = _orbitAngles.y;
        float const x = _cameraDistance * glm::cos(pitch) * glm::sin(yaw);
        float const y = _cameraDistance * glm::sin(pitch);
        float const z = _cameraDistance * glm::cos(pitch) * glm::cos(yaw);
        return _cameraTarget + glm::vec3(x, y, z);
    }

    glm::vec3 OrbitCamera::Target() const {
        return _cameraTarget;
    }

    void OrbitCamera::SetTarget(const glm::vec3 & target) {
        _cameraTarget = target;
    }

    float OrbitCamera::Distance() const {
        return _cameraDistance;
    }

    void OrbitCamera::SetDistance(float distance) {
        _cameraDistance = std::clamp(distance, 1.2f, 14.0f);
    }

    glm::vec2 OrbitCamera::Angles() const {
        return _orbitAngles;
    }

    void OrbitCamera::SetAngles(const glm::vec2 & angles) {
        _orbitAngles = angles;
    }

    bool OrbitCamera::AutoRotate() const {
        return _autoRotate;
    }

    void OrbitCamera::SetAutoRotate(bool enabled) {
        _autoRotate = enabled;
    }
} // namespace VCX::Apps::VolumeFX
