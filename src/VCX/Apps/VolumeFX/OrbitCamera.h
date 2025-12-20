#pragma once

#include <glm/glm.hpp>

struct ImGuiIO;

namespace VCX::Apps::VolumeFX {
    class OrbitCamera {
    public:
        OrbitCamera();

        void Update(const ImGuiIO & io, float deltaTime);

        glm::vec3 Position() const;
        glm::vec3 Target() const;
        void SetTarget(const glm::vec3 & target);

        float Distance() const;
        void SetDistance(float distance);

        glm::vec2 Angles() const;
        void SetAngles(const glm::vec2 & angles);

        bool AutoRotate() const;
        void SetAutoRotate(bool enabled);

    private:
        glm::vec2 _orbitAngles { 0.6f, 0.35f };
        float     _cameraDistance = 4.0f;
        glm::vec3 _cameraTarget { 0.0f, 0.0f, 0.0f };
        bool      _autoRotate = true;
        bool      _isOrbiting = false;
    };
} // namespace VCX::Apps::VolumeFX
