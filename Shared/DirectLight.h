#pragma once

#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace RtPbrSurvey
{
constexpr uint32_t kMaxDirectLights = 16;
constexpr uint32_t kNoShadowLight = 0xffffffffu;

enum class LightType : uint32_t
{
    Directional,
    Point,
    Spot
};

struct DirectLight
{
    uint32_t id = 1;
    std::string name = "Directional 1";
    LightType type = LightType::Directional;
    bool enabled = true;
    DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    DirectX::XMFLOAT3 position = {0.0f, 2.0f, 0.0f};
    // World direction in which light travels (opposite to surface-to-light).
    DirectX::XMFLOAT3 direction = {0.0f, -0.70710678f, 0.70710678f};
    float range = 10.0f;
    float innerCone = 20.0f;
    float outerCone = 30.0f;
};

struct alignas(16) LightGpuData
{
    DirectX::XMFLOAT3 position;
    float range;
    DirectX::XMFLOAT3 direction;
    uint32_t type;
    DirectX::XMFLOAT3 radiance;
    float innerCos;
    float outerCos;
    uint32_t enabled;
    uint32_t padding[2] = {};
};
static_assert(sizeof(LightGpuData) == 64);
static_assert(offsetof(LightGpuData, direction) == 16);
static_assert(offsetof(LightGpuData, radiance) == 32);
static_assert(offsetof(LightGpuData, outerCos) == 48);

inline bool SameFloat3(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline bool operator==(const DirectLight& a, const DirectLight& b)
{
    return a.id == b.id && a.name == b.name && a.type == b.type && a.enabled == b.enabled &&
           SameFloat3(a.color, b.color) && a.intensity == b.intensity && SameFloat3(a.position, b.position) &&
           SameFloat3(a.direction, b.direction) && a.range == b.range && a.innerCone == b.innerCone &&
           a.outerCone == b.outerCone;
}

inline void ValidateDirectLight(DirectLight& light)
{
    const auto finite3 = [](const DirectX::XMFLOAT3& v)
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const double length = std::sqrt(static_cast<double>(light.direction.x) * light.direction.x +
                                    static_cast<double>(light.direction.y) * light.direction.y +
                                    static_cast<double>(light.direction.z) * light.direction.z);
    if (light.id == 0 || static_cast<uint32_t>(light.type) > 2 || !finite3(light.color) || light.color.x < 0 ||
        light.color.y < 0 || light.color.z < 0 || !std::isfinite(light.intensity) || light.intensity < 0 ||
        !finite3(light.position) || !finite3(light.direction) || length <= 1e-8 || !std::isfinite(light.range) ||
        light.range <= 0 || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) || light.innerCone < 0 ||
        light.innerCone >= light.outerCone || light.outerCone >= 90 ||
        !std::isfinite(light.color.x * light.intensity) || !std::isfinite(light.color.y * light.intensity) ||
        !std::isfinite(light.color.z * light.intensity))
    {
        throw std::runtime_error("Invalid light: require nonzero ID/direction, finite nonnegative radiance, positive "
                                 "range, and 0 <= inner < outer < 90 degrees.");
    }
    // Avoid repeated rounding changes when settings are applied every frame.
    if (std::abs(length - 1.0) > 1e-6)
    {
        light.direction = {static_cast<float>(light.direction.x / length),
                           static_cast<float>(light.direction.y / length),
                           static_cast<float>(light.direction.z / length)};
    }
}

inline void ValidateDirectLights(std::vector<DirectLight>& lights)
{
    if (lights.size() > kMaxDirectLights)
    {
        throw std::runtime_error("At most 16 direct lights are supported.");
    }
    for (size_t i = 0; i < lights.size(); ++i)
    {
        ValidateDirectLight(lights[i]);
        for (size_t j = 0; j < i; ++j)
        {
            if (lights[i].id == lights[j].id)
            {
                throw std::runtime_error("Duplicate light ID.");
            }
        }
    }
}

inline const DirectLight* FindShadowLight(const std::vector<DirectLight>& lights, uint32_t id)
{
    for (const DirectLight& light : lights)
    {
        if (light.id == id && light.enabled && light.type == LightType::Directional)
        {
            return &light;
        }
    }
    return nullptr;
}

inline DirectX::XMFLOAT3 ShadowLightDirection(const std::vector<DirectLight>& lights, uint32_t id)
{
    const DirectLight* light = FindShadowLight(lights, id);
    return light ? DirectX::XMFLOAT3{-light->direction.x, -light->direction.y, -light->direction.z}
                 : DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
}

inline LightGpuData MakeLightGpuData(const DirectLight& light)
{
    return {light.position,
            light.range,
            light.direction,
            static_cast<uint32_t>(light.type),
            {light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity},
            std::cos(DirectX::XMConvertToRadians(light.innerCone)),
            std::cos(DirectX::XMConvertToRadians(light.outerCone)),
            light.enabled ? 1u : 0u,
            {0, 0}};
}
} // namespace RtPbrSurvey
