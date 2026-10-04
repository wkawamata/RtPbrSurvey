#include "stdafx.h"

#include "Scene/SceneDocumentJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace RtPbrSurvey
{
namespace
{
using json = nlohmann::json;

bool IsFinite(float value)
{
    return std::isfinite(value);
}

bool IsFinite(const SceneFloat3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const SceneFloat4& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

bool IsRelativePath(const std::string& path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\')
    {
        return false;
    }
    return path.size() < 2 || path[1] != ':';
}

bool IsKnownFields(const json& value, std::initializer_list<const char*> fields)
{
    if (!value.is_object())
    {
        return false;
    }
    std::unordered_set<std::string> known;
    for (const char* field : fields)
    {
        known.insert(field);
    }
    for (auto item = value.cbegin(); item != value.cend(); ++item)
    {
        if (!known.contains(item.key()))
        {
            return false;
        }
    }
    return true;
}

bool RequireString(const json& value, const char* name, std::string& destination, std::string* error)
{
    if (!value.contains(name) || !value.at(name).is_string())
    {
        if (error != nullptr)
        {
            *error = std::string("Missing or invalid string: ") + name;
        }
        return false;
    }
    destination = value.at(name).get<std::string>();
    return true;
}

bool Float3FromJson(const json& value, SceneFloat3& destination)
{
    if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number())
    {
        return false;
    }
    destination = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return IsFinite(destination);
}

bool Float4FromJson(const json& value, SceneFloat4& destination)
{
    if (!value.is_array() || value.size() != 4 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number() || !value[3].is_number())
    {
        return false;
    }
    destination = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
    return IsFinite(destination);
}

json Float3ToJson(const SceneFloat3& value)
{
    return {value.x, value.y, value.z};
}

json Float4ToJson(const SceneFloat4& value)
{
    return {value.x, value.y, value.z, value.w};
}

const char* NodeTypeName(SceneNodeType type)
{
    switch (type)
    {
    case SceneNodeType::Empty:
        return "empty";
    case SceneNodeType::Gltf:
        return "gltf";
    case SceneNodeType::Primitive:
        return "primitive";
    }
    return "unknown";
}

bool NodeTypeFromJson(const json& value, SceneNodeType& type)
{
    if (!value.is_string())
    {
        return false;
    }
    const std::string name = value.get<std::string>();
    if (name == "empty")
    {
        type = SceneNodeType::Empty;
        return true;
    }
    if (name == "gltf")
    {
        type = SceneNodeType::Gltf;
        return true;
    }
    if (name == "primitive")
    {
        type = SceneNodeType::Primitive;
        return true;
    }
    return false;
}

const char* PrimitiveKindName(ScenePrimitiveKind kind)
{
    switch (kind)
    {
    case ScenePrimitiveKind::Cube:
        return "cube";
    case ScenePrimitiveKind::Sphere:
        return "sphere";
    case ScenePrimitiveKind::Plane:
        return "plane";
    case ScenePrimitiveKind::Cylinder:
        return "cylinder";
    }
    return "unknown";
}

bool PrimitiveKindFromJson(const json& value, ScenePrimitiveKind& kind)
{
    if (!value.is_string())
    {
        return false;
    }
    const std::string name = value.get<std::string>();
    if (name == "cube")
    {
        kind = ScenePrimitiveKind::Cube;
        return true;
    }
    if (name == "sphere")
    {
        kind = ScenePrimitiveKind::Sphere;
        return true;
    }
    if (name == "plane")
    {
        kind = ScenePrimitiveKind::Plane;
        return true;
    }
    if (name == "cylinder")
    {
        kind = ScenePrimitiveKind::Cylinder;
        return true;
    }
    return false;
}

bool ParseTransform(const json& value, SceneTransform& transform)
{
    return IsKnownFields(value, {"translation", "rotation", "scale"}) && value.contains("translation") &&
           value.contains("rotation") && value.contains("scale") &&
           Float3FromJson(value.at("translation"), transform.translation) &&
           Float4FromJson(value.at("rotation"), transform.rotation) && Float3FromJson(value.at("scale"), transform.scale);
}

json TransformToJson(const SceneTransform& transform)
{
    return {{"translation", Float3ToJson(transform.translation)},
            {"rotation", Float4ToJson(transform.rotation)},
            {"scale", Float3ToJson(transform.scale)}};
}

bool ParsePrimitive(const json& value, ScenePrimitive& primitive)
{
    if (!IsKnownFields(value, {"kind", "size", "radius", "height", "width", "depth", "stacks", "slices", "radialSegments"}) ||
        !value.contains("kind") || !PrimitiveKindFromJson(value.at("kind"), primitive.kind))
    {
        return false;
    }
    primitive.size = value.value("size", primitive.size);
    primitive.radius = value.value("radius", primitive.radius);
    primitive.height = value.value("height", primitive.height);
    primitive.width = value.value("width", primitive.width);
    primitive.depth = value.value("depth", primitive.depth);
    primitive.stacks = value.value("stacks", primitive.stacks);
    primitive.slices = value.value("slices", primitive.slices);
    primitive.radialSegments = value.value("radialSegments", primitive.radialSegments);
    return true;
}

json PrimitiveToJson(const ScenePrimitive& primitive)
{
    return {{"kind", PrimitiveKindName(primitive.kind)},
            {"size", primitive.size},
            {"radius", primitive.radius},
            {"height", primitive.height},
            {"width", primitive.width},
            {"depth", primitive.depth},
            {"stacks", primitive.stacks},
            {"slices", primitive.slices},
            {"radialSegments", primitive.radialSegments}};
}

bool ParseNode(const json& value, SceneNode& node, std::string* error)
{
    if (!IsKnownFields(value,
                       {"id", "name", "parentId", "type", "translation", "rotation", "scale", "visible", "assetId", "primitive", "materialId"}) ||
        !RequireString(value, "id", node.id, error) || !RequireString(value, "name", node.name, error) ||
        !value.contains("type") || !NodeTypeFromJson(value.at("type"), node.type) || !value.contains("translation") ||
        !value.contains("rotation") || !value.contains("scale") ||
        !ParseTransform({{"translation", value.at("translation")},
                         {"rotation", value.at("rotation")},
                         {"scale", value.at("scale")}},
                        node.transform) ||
        !value.contains("visible") || !value.at("visible").is_boolean())
    {
        if (error != nullptr && error->empty())
        {
            *error = "Invalid node.";
        }
        return false;
    }
    node.visible = value.at("visible").get<bool>();
    if (value.contains("parentId") && !value.at("parentId").is_null())
    {
        if (!value.at("parentId").is_string())
        {
            if (error != nullptr)
            {
                *error = "Invalid node parentId.";
            }
            return false;
        }
        node.parentId = value.at("parentId").get<std::string>();
    }
    if (node.type == SceneNodeType::Gltf)
    {
        return RequireString(value, "assetId", node.assetId, error);
    }
    if (node.type == SceneNodeType::Primitive)
    {
        return value.contains("primitive") && ParsePrimitive(value.at("primitive"), node.primitive) &&
               RequireString(value, "materialId", node.materialId, error);
    }
    return true;
}

json NodeToJson(const SceneNode& node)
{
    json value = {{"id", node.id},
                  {"name", node.name},
                  {"parentId", node.parentId.has_value() ? json(*node.parentId) : json(nullptr)},
                  {"type", NodeTypeName(node.type)},
                  {"translation", Float3ToJson(node.transform.translation)},
                  {"rotation", Float4ToJson(node.transform.rotation)},
                  {"scale", Float3ToJson(node.transform.scale)},
                  {"visible", node.visible}};
    if (node.type == SceneNodeType::Gltf)
    {
        value["assetId"] = node.assetId;
    }
    else if (node.type == SceneNodeType::Primitive)
    {
        value["primitive"] = PrimitiveToJson(node.primitive);
        value["materialId"] = node.materialId;
    }
    return value;
}

} // namespace

bool ValidateSceneDocument(const SceneDocument& document, std::string* error)
{
    const auto fail = [error](const std::string& message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (document.schemaVersion != SceneDocument::kSchemaVersion || document.sceneId.empty() || document.name.empty() ||
        !IsRelativePath(document.renderPresetPath) || !HasUniqueSceneDocumentIds(document))
    {
        return fail("Scene document header is invalid.");
    }

    std::unordered_set<std::string> assetIds;
    for (const SceneAsset& asset : document.assets)
    {
        if (!IsRelativePath(asset.path))
        {
            return fail("Asset path is not relative: " + asset.id);
        }
        assetIds.insert(asset.id);
    }
    std::unordered_set<std::string> materialIds;
    for (const SceneMaterial& material : document.materials)
    {
        if (!IsFinite(material.baseColor) || !IsFinite(material.metallic) || !IsFinite(material.roughness) ||
            material.baseColor.x < 0.0f || material.baseColor.x > 1.0f || material.baseColor.y < 0.0f ||
            material.baseColor.y > 1.0f || material.baseColor.z < 0.0f || material.baseColor.z > 1.0f ||
            material.baseColor.w != 1.0f || material.metallic < 0.0f || material.metallic > 1.0f ||
            material.roughness < 0.0f || material.roughness > 1.0f)
        {
            return fail("Material values are invalid: " + material.id);
        }
        materialIds.insert(material.id);
    }
    for (const SceneNode& node : document.nodes)
    {
        if (!IsFinite(node.transform.translation) || !IsFinite(node.transform.rotation) || !IsFinite(node.transform.scale) ||
            node.transform.scale.x <= 0.0f || node.transform.scale.y <= 0.0f || node.transform.scale.z <= 0.0f)
        {
            return fail("Node transform is invalid: " + node.id);
        }
        if (node.type == SceneNodeType::Gltf && !assetIds.contains(node.assetId))
        {
            return fail("Node asset reference is invalid: " + node.id);
        }
        if (node.type == SceneNodeType::Primitive)
        {
            if (!materialIds.contains(node.materialId))
            {
                return fail("Node material reference is invalid: " + node.id);
            }
            const ScenePrimitive& primitive = node.primitive;
            if (!IsFinite(primitive.size) || !IsFinite(primitive.radius) || !IsFinite(primitive.height) ||
                !IsFinite(primitive.width) || !IsFinite(primitive.depth) || primitive.size <= 0.0f ||
                primitive.radius <= 0.0f || primitive.height <= 0.0f || primitive.width <= 0.0f ||
                primitive.depth <= 0.0f || primitive.stacks < 2 || primitive.slices < 3 || primitive.radialSegments < 3)
            {
                return fail("Primitive values are invalid: " + node.id);
            }
        }
    }
    if (!IsFinite(document.camera.position) || !IsFinite(document.camera.target) || !IsFinite(document.camera.up) ||
        !IsFinite(document.camera.verticalFovDegrees) || !IsFinite(document.camera.orthographicHeight) ||
        !IsFinite(document.camera.nearZ) || !IsFinite(document.camera.farZ) || document.camera.nearZ <= 0.0f ||
        document.camera.farZ <= document.camera.nearZ || document.camera.verticalFovDegrees <= 0.0f ||
        document.camera.verticalFovDegrees >= 180.0f || document.camera.orthographicHeight <= 0.0f)
    {
        return fail("Camera values are invalid.");
    }
    if (!IsFinite(document.environment.skyColor) || !IsFinite(document.environment.groundColor) ||
        !IsFinite(document.environment.lightColor) || !IsFinite(document.environment.lightDirection) ||
        !IsFinite(document.environment.backgroundIntensity) || !IsFinite(document.environment.lightIntensity) ||
        !IsFinite(document.environment.lightSize) || !IsFinite(document.environment.fillIntensity) ||
        !IsFinite(document.environment.colorPanelIntensity) || !IsFinite(document.environment.horizonSharpness))
    {
        return fail("Environment values are invalid.");
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SerializeSceneDocument(const SceneDocument& document, std::string& jsonText, std::string* error, int indent)
{
    if (!ValidateSceneDocument(document, error))
    {
        return false;
    }

    json assets = json::array();
    for (const SceneAsset& asset : document.assets)
    {
        assets.push_back({{"id", asset.id}, {"type", "gltf"}, {"path", asset.path}});
    }
    json materials = json::array();
    for (const SceneMaterial& material : document.materials)
    {
        materials.push_back({{"id", material.id},
                             {"name", material.name},
                             {"baseColor", Float4ToJson(material.baseColor)},
                             {"metallic", material.metallic},
                             {"roughness", material.roughness}});
    }
    json nodes = json::array();
    for (const SceneNode& node : document.nodes)
    {
        nodes.push_back(NodeToJson(node));
    }

    const json root = {{"schemaVersion", document.schemaVersion},
                       {"sceneId", document.sceneId},
                       {"name", document.name},
                       {"renderPreset", document.renderPresetPath},
                       {"assets", std::move(assets)},
                       {"materials", std::move(materials)},
                       {"nodes", std::move(nodes)},
                       {"camera",
                        {{"position", Float3ToJson(document.camera.position)},
                         {"target", Float3ToJson(document.camera.target)},
                         {"up", Float3ToJson(document.camera.up)},
                         {"projection", document.camera.projection == SceneCameraProjection::Perspective ? "perspective" : "orthographic"},
                         {"verticalFovDegrees", document.camera.verticalFovDegrees},
                         {"orthographicHeight", document.camera.orthographicHeight},
                         {"nearZ", document.camera.nearZ},
                         {"farZ", document.camera.farZ}}},
                       {"environment",
                        {{"source", "procedural"},
                         {"iblEnabled", document.environment.iblEnabled},
                         {"skyColor", Float3ToJson(document.environment.skyColor)},
                         {"groundColor", Float3ToJson(document.environment.groundColor)},
                         {"lightColor", Float3ToJson(document.environment.lightColor)},
                         {"lightDirection", Float3ToJson(document.environment.lightDirection)},
                         {"backgroundIntensity", document.environment.backgroundIntensity},
                         {"lightIntensity", document.environment.lightIntensity},
                         {"lightSize", document.environment.lightSize},
                         {"fillIntensity", document.environment.fillIntensity},
                         {"colorPanelIntensity", document.environment.colorPanelIntensity},
                         {"horizonSharpness", document.environment.horizonSharpness}}}};
    jsonText = root.dump(indent, ' ', false, json::error_handler_t::replace);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool DeserializeSceneDocument(std::string_view jsonText, SceneDocument& document, std::string* error)
{
    try
    {
        const json root = json::parse(jsonText);
        if (!IsKnownFields(root, {"schemaVersion", "sceneId", "name", "renderPreset", "assets", "materials", "nodes", "camera", "environment"}) ||
            !root.contains("schemaVersion") || !root.at("schemaVersion").is_number_integer() ||
            root.at("schemaVersion").get<int>() != SceneDocument::kSchemaVersion)
        {
            if (error != nullptr)
            {
                *error = "Unsupported or invalid scene document schema.";
            }
            return false;
        }

        SceneDocument parsed;
        parsed.schemaVersion = root.at("schemaVersion").get<int>();
        if (!RequireString(root, "sceneId", parsed.sceneId, error) || !RequireString(root, "name", parsed.name, error) ||
            !RequireString(root, "renderPreset", parsed.renderPresetPath, error) || !root.contains("assets") ||
            !root.at("assets").is_array() || !root.contains("materials") || !root.at("materials").is_array() ||
            !root.contains("nodes") || !root.at("nodes").is_array())
        {
            return false;
        }
        for (const json& assetValue : root.at("assets"))
        {
            SceneAsset asset;
            std::string type;
            if (!IsKnownFields(assetValue, {"id", "type", "path"}) || !RequireString(assetValue, "id", asset.id, error) ||
                !RequireString(assetValue, "type", type, error) || type != "gltf" ||
                !RequireString(assetValue, "path", asset.path, error))
            {
                return false;
            }
            parsed.assets.push_back(std::move(asset));
        }
        for (const json& materialValue : root.at("materials"))
        {
            SceneMaterial material;
            if (!IsKnownFields(materialValue, {"id", "name", "baseColor", "metallic", "roughness"}) ||
                !RequireString(materialValue, "id", material.id, error) || !RequireString(materialValue, "name", material.name, error) ||
                !materialValue.contains("baseColor") || !Float4FromJson(materialValue.at("baseColor"), material.baseColor) ||
                !materialValue.contains("metallic") || !materialValue.at("metallic").is_number() ||
                !materialValue.contains("roughness") || !materialValue.at("roughness").is_number())
            {
                if (error != nullptr && error->empty())
                {
                    *error = "Invalid material.";
                }
                return false;
            }
            material.metallic = materialValue.at("metallic").get<float>();
            material.roughness = materialValue.at("roughness").get<float>();
            parsed.materials.push_back(std::move(material));
        }
        for (const json& nodeValue : root.at("nodes"))
        {
            SceneNode node;
            if (!ParseNode(nodeValue, node, error))
            {
                return false;
            }
            parsed.nodes.push_back(std::move(node));
        }
        if (!root.contains("camera") || !IsKnownFields(root.at("camera"), {"position", "target", "up", "projection", "verticalFovDegrees", "orthographicHeight", "nearZ", "farZ"}) ||
            !root.at("camera").contains("position") || !Float3FromJson(root.at("camera").at("position"), parsed.camera.position) ||
            !root.at("camera").contains("target") || !Float3FromJson(root.at("camera").at("target"), parsed.camera.target) ||
            !root.at("camera").contains("up") || !Float3FromJson(root.at("camera").at("up"), parsed.camera.up) ||
            !root.at("camera").contains("projection") || !root.at("camera").at("projection").is_string() ||
            !root.at("camera").contains("verticalFovDegrees") || !root.at("camera").at("verticalFovDegrees").is_number() ||
            !root.at("camera").contains("orthographicHeight") || !root.at("camera").at("orthographicHeight").is_number() ||
            !root.at("camera").contains("nearZ") || !root.at("camera").at("nearZ").is_number() ||
            !root.at("camera").contains("farZ") || !root.at("camera").at("farZ").is_number())
        {
            if (error != nullptr)
            {
                *error = "Invalid camera.";
            }
            return false;
        }
        const std::string projection = root.at("camera").at("projection").get<std::string>();
        if (projection != "perspective" && projection != "orthographic")
        {
            if (error != nullptr)
            {
                *error = "Unknown camera projection.";
            }
            return false;
        }
        parsed.camera.projection = projection == "perspective" ? SceneCameraProjection::Perspective : SceneCameraProjection::Orthographic;
        parsed.camera.verticalFovDegrees = root.at("camera").at("verticalFovDegrees").get<float>();
        parsed.camera.orthographicHeight = root.at("camera").at("orthographicHeight").get<float>();
        parsed.camera.nearZ = root.at("camera").at("nearZ").get<float>();
        parsed.camera.farZ = root.at("camera").at("farZ").get<float>();

        if (!root.contains("environment") ||
            !IsKnownFields(root.at("environment"), {"source", "iblEnabled", "skyColor", "groundColor", "lightColor", "lightDirection", "backgroundIntensity", "lightIntensity", "lightSize", "fillIntensity", "colorPanelIntensity", "horizonSharpness"}) ||
            !root.at("environment").contains("source") || root.at("environment").at("source") != "procedural" ||
            !root.at("environment").contains("iblEnabled") || !root.at("environment").at("iblEnabled").is_boolean() ||
            !root.at("environment").contains("skyColor") || !Float3FromJson(root.at("environment").at("skyColor"), parsed.environment.skyColor) ||
            !root.at("environment").contains("groundColor") || !Float3FromJson(root.at("environment").at("groundColor"), parsed.environment.groundColor) ||
            !root.at("environment").contains("lightColor") || !Float3FromJson(root.at("environment").at("lightColor"), parsed.environment.lightColor) ||
            !root.at("environment").contains("lightDirection") || !Float3FromJson(root.at("environment").at("lightDirection"), parsed.environment.lightDirection))
        {
            if (error != nullptr)
            {
                *error = "Invalid environment.";
            }
            return false;
        }
        parsed.environment.iblEnabled = root.at("environment").at("iblEnabled").get<bool>();
        const std::initializer_list<std::pair<const char*, float*>> environmentNumbers = {
            {"backgroundIntensity", &parsed.environment.backgroundIntensity},
            {"lightIntensity", &parsed.environment.lightIntensity},
            {"lightSize", &parsed.environment.lightSize},
            {"fillIntensity", &parsed.environment.fillIntensity},
            {"colorPanelIntensity", &parsed.environment.colorPanelIntensity},
            {"horizonSharpness", &parsed.environment.horizonSharpness},
        };
        for (const auto& [name, destination] : environmentNumbers)
        {
            if (!root.at("environment").contains(name) || !root.at("environment").at(name).is_number())
            {
                if (error != nullptr)
                {
                    *error = std::string("Invalid environment value: ") + name;
                }
                return false;
            }
            *destination = root.at("environment").at(name).get<float>();
        }
        if (!ValidateSceneDocument(parsed, error))
        {
            return false;
        }
        document = std::move(parsed);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
        {
            *error = exception.what();
        }
        return false;
    }
}

bool LoadSceneDocumentFile(const std::string& path, SceneDocument& document, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        if (error != nullptr)
        {
            *error = "Could not open scene document: " + path;
        }
        return false;
    }
    const std::string jsonText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return DeserializeSceneDocument(jsonText, document, error);
}

bool SaveSceneDocumentFile(const std::string& path, const SceneDocument& document, std::string* error)
{
    if (path.empty())
    {
        if (error != nullptr)
        {
            *error = "Scene document path is empty.";
        }
        return false;
    }
    std::string jsonText;
    if (!SerializeSceneDocument(document, jsonText, error))
    {
        return false;
    }

    const std::string temporaryPath = path + ".tmp";
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        if (error != nullptr)
        {
            *error = "Could not open temporary scene document: " + temporaryPath;
        }
        return false;
    }
    file << jsonText;
    file.close();
    if (!file)
    {
        DeleteFileA(temporaryPath.c_str());
        if (error != nullptr)
        {
            *error = "Could not write scene document: " + temporaryPath;
        }
        return false;
    }
    if (!MoveFileExA(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(temporaryPath.c_str());
        if (error != nullptr)
        {
            *error = "Could not replace scene document: " + path;
        }
        return false;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool RebaseSceneDocumentPaths(SceneDocument& document,
                              const std::filesystem::path& oldSceneDirectory,
                              const std::filesystem::path& newSceneDirectory,
                              std::string* error)
{
    if (oldSceneDirectory.empty() || newSceneDirectory.empty())
    {
        if (error != nullptr)
        {
            *error = "Scene directories must not be empty when rebasing paths.";
        }
        return false;
    }

    const std::filesystem::path oldDirectory = std::filesystem::absolute(oldSceneDirectory).lexically_normal();
    const std::filesystem::path newDirectory = std::filesystem::absolute(newSceneDirectory).lexically_normal();
    SceneDocument rebased = document;
    auto rebasePath = [&oldDirectory, &newDirectory](const std::string& path, std::string& result)
    {
        const std::filesystem::path relativePath(path);
        if (relativePath.empty() || relativePath.is_absolute())
        {
            return false;
        }
        const std::filesystem::path originalPath = (oldDirectory / relativePath).lexically_normal();
        const std::filesystem::path rebasedPath = originalPath.lexically_relative(newDirectory);
        if (rebasedPath.empty() || rebasedPath.is_absolute())
        {
            return false;
        }
        result = rebasedPath.generic_string();
        return true;
    };

    if (!rebasePath(document.renderPresetPath, rebased.renderPresetPath))
    {
        if (error != nullptr)
        {
            *error = "Could not rebase renderPreset path: " + document.renderPresetPath;
        }
        return false;
    }
    for (size_t index = 0; index < document.assets.size(); ++index)
    {
        if (!rebasePath(document.assets[index].path, rebased.assets[index].path))
        {
            if (error != nullptr)
            {
                *error = "Could not rebase asset path for '" + document.assets[index].id + "': " +
                         document.assets[index].path;
            }
            return false;
        }
    }

    document = std::move(rebased);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

} // namespace RtPbrSurvey
