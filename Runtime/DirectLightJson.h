#pragma once

#include "Shared/DirectLight.h"

#include <nlohmann/json.hpp>

namespace RtPbrSurvey
{
inline nlohmann::json DirectLightsToJson(const std::vector<DirectLight>& lights)
{
    nlohmann::json result = nlohmann::json::array();
    for (const DirectLight& light : lights)
    {
        result.push_back({{"id", light.id},
                          {"name", light.name},
                          {"type",
                           light.type == LightType::Directional ? "directional"
                           : light.type == LightType::Point     ? "point"
                                                                : "spot"},
                          {"enabled", light.enabled},
                          {"color", {light.color.x, light.color.y, light.color.z}},
                          {"intensity", light.intensity},
                          {"position", {light.position.x, light.position.y, light.position.z}},
                          {"direction", {light.direction.x, light.direction.y, light.direction.z}},
                          {"range", light.range},
                          {"innerCone", light.innerCone},
                          {"outerCone", light.outerCone}});
    }
    return result;
}

inline uint32_t LightIdFromJson(const nlohmann::json& value)
{
    if ((!value.is_number_unsigned() && !value.is_number_integer()) || value < 0 || value > UINT32_MAX)
    {
        throw std::runtime_error("Light ID must be an unsigned 32-bit integer.");
    }
    return value.get<uint32_t>();
}

inline DirectX::XMFLOAT3 LightFloat3FromJson(const nlohmann::json& value)
{
    if (!value.is_array() || value.size() != 3)
    {
        throw std::runtime_error("Light vector must contain three numbers.");
    }
    return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
}

inline std::vector<DirectLight> DirectLightsFromJson(const nlohmann::json& value)
{
    if (!value.is_array() || value.size() > kMaxDirectLights)
    {
        throw std::runtime_error("lights must be an array of at most 16 lights.");
    }
    std::vector<DirectLight> lights;
    for (const nlohmann::json& entry : value)
    {
        DirectLight light;
        light.id = LightIdFromJson(entry.at("id"));
        light.name = entry.at("name").get<std::string>();
        const std::string type = entry.at("type").get<std::string>();
        if (type != "directional" && type != "point" && type != "spot")
        {
            throw std::runtime_error("Unknown light type: " + type);
        }
        light.type = type == "directional" ? LightType::Directional
                     : type == "point"     ? LightType::Point
                                           : LightType::Spot;
        light.enabled = entry.at("enabled").get<bool>();
        light.color = LightFloat3FromJson(entry.at("color"));
        light.intensity = entry.at("intensity").get<float>();
        light.position = LightFloat3FromJson(entry.at("position"));
        light.direction = LightFloat3FromJson(entry.at("direction"));
        light.range = entry.at("range").get<float>();
        light.innerCone = entry.at("innerCone").get<float>();
        light.outerCone = entry.at("outerCone").get<float>();
        lights.push_back(std::move(light));
    }
    ValidateDirectLights(lights);
    return lights;
}
} // namespace RtPbrSurvey
