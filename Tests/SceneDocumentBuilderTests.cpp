#include "Scene/SceneDocumentBuilder.h"
#include "Scene/SceneDocumentJson.h"

#include <DirectXMath.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include "Scene/SceneDocumentRuntimeScene.h"
#include "Scene/ImportedVolumeScene.h"

namespace
{

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

RtPbrSurvey::SceneDocument CreatePrimitiveDocument()
{
    RtPbrSurvey::SceneDocument document = RtPbrSurvey::CreateEmptySceneDocument("primitive-test", "Primitive Test");
    document.renderPresetPath = "RenderPresets/default.json";
    document.camera.lensShiftX = 0.35f;
    document.camera.lensShiftY = -0.2f;
    document.materials = {
        {"metal", "Metal", {0.8f, 0.4f, 0.2f, 1.0f}, 1.0f, 0.2f},
        {"floor", "Floor", {0.3f, 0.3f, 0.3f, 1.0f}, 0.0f, 0.8f},
    };

    RtPbrSurvey::SceneNode cube;
    cube.id = "cube";
    cube.type = RtPbrSurvey::SceneNodeType::Primitive;
    cube.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Cube;
    cube.primitive.size = 2.0f;
    cube.materialId = "metal";
    cube.transform.translation = {1.0f, 0.0f, 0.0f};

    RtPbrSurvey::SceneNode duplicateCube = cube;
    duplicateCube.id = "cube-copy";
    duplicateCube.transform.translation = {-1.0f, 0.0f, 0.0f};

    RtPbrSurvey::SceneNode plane;
    plane.id = "plane";
    plane.type = RtPbrSurvey::SceneNodeType::Primitive;
    plane.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Plane;
    plane.primitive.width = 8.0f;
    plane.primitive.depth = 6.0f;
    plane.materialId = "floor";

    RtPbrSurvey::SceneNode sphere;
    sphere.id = "sphere";
    sphere.type = RtPbrSurvey::SceneNodeType::Primitive;
    sphere.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Sphere;
    sphere.primitive.radius = 0.5f;
    sphere.materialId = "metal";

    RtPbrSurvey::SceneNode cylinder;
    cylinder.id = "cylinder";
    cylinder.type = RtPbrSurvey::SceneNodeType::Primitive;
    cylinder.primitive.kind = RtPbrSurvey::ScenePrimitiveKind::Cylinder;
    cylinder.primitive.radius = 0.25f;
    cylinder.primitive.height = 2.0f;
    cylinder.primitive.radialSegments = 16;
    cylinder.materialId = "metal";

    document.nodes = {cube, duplicateCube, plane, sphere, cylinder};
    return document;
}

bool TestPrimitiveBuildAndMeshReuse()
{
    RtPbrSurvey::SceneDocumentBuilder documentBuilder;
    std::string error;
    const RtPbrSurvey::SceneDocument document = CreatePrimitiveDocument();
    if (!Check(documentBuilder.Build(document, &error), "primitive document builds"))
    {
        return false;
    }

    const Engine::SceneBuilder& builder = documentBuilder.Builder();
    const Engine::SceneMesh& mesh = builder.GetMesh();
    const Engine::Scene& scene = builder.GetScene();
    const RtPbrSurvey::SceneDocumentBuildResult& result = documentBuilder.Result();
    bool passed = Check(scene.instances.size() == 5, "one instance is built per visible primitive");
    passed &= Check(NearlyEqual(scene.camera.lensShiftX, 0.35f) && NearlyEqual(scene.camera.lensShiftY, -0.2f),
                    "lens shift reaches runtime camera");
    passed &= Check(mesh.ranges.size() == 4, "identical primitive parameters reuse a mesh range");
    passed &= Check(scene.instances[0].meshId == scene.instances[1].meshId, "duplicate cubes share a mesh ID");
    passed &= Check(result.nodeInstanceIndices.size() == 5 && result.materialIds.size() == 2,
                    "node and material IDs map to renderer indices");
    passed &= Check(mesh.materials[result.materialIds.at("metal")].metallicFactor == 1.0f &&
                        NearlyEqual(mesh.materials[result.materialIds.at("metal")].roughnessFactor, 0.2f),
                    "PBR material factors are preserved");
    for (const Engine::SceneMaterial& material : mesh.materials)
    {
        const Engine::SceneTexture& data = mesh.textures.at(material.metallicRoughnessTexIndex);
        const Engine::SceneTexture& emission = mesh.textures.at(material.emissiveTexIndex);
        passed &= Check(material.albedoTexIndex != material.metallicRoughnessTexIndex &&
                            data.colorSpace == Engine::TextureColorSpace::Linear &&
                            data.pixels == std::vector<unsigned char>({255, 255, 255, 255}),
                        "scalar metallic and roughness use neutral linear data, independent of base color");
        passed &= Check(material.occlusionTexIndex == material.metallicRoughnessTexIndex &&
                            emission.pixels == std::vector<unsigned char>({0, 0, 0, 255}),
                        "document materials have neutral occlusion and no implicit emission");
    }
    const Engine::SceneMesh::Range& planeRange = mesh.ranges[scene.instances[2].meshId];
    passed &= Check(planeRange.vertexCount == 4 && mesh.vertices[planeRange.firstVertex].normal.y == 1.0f,
                    "plane is generated on XZ with an upward normal");
    DirectX::XMFLOAT4X4 cubeWorld;
    DirectX::XMStoreFloat4x4(&cubeWorld, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&scene.instances[0].world)));
    passed &= Check(NearlyEqual(cubeWorld._41, 1.0f), "scene graph world transform reaches the instance");
    return passed;
}

bool TestGltfBuildAndMeshReuse()
{
    RtPbrSurvey::SceneDocument document = CreatePrimitiveDocument();
    document.assets.push_back({"fixture", "Gltf/multi-node.gltf"});
    RtPbrSurvey::SceneNode first;
    first.id = "gltf-first";
    first.type = RtPbrSurvey::SceneNodeType::Gltf;
    first.assetId = "fixture";
    first.transform.translation = {-3.0f, 0.0f, 0.0f};
    RtPbrSurvey::SceneNode second = first;
    second.id = "gltf-second";
    second.transform.translation = {3.0f, 0.0f, 0.0f};
    document.nodes.push_back(first);
    document.nodes.push_back(second);

    RtPbrSurvey::SceneDocumentBuilder documentBuilder;
    std::string error;
    const std::filesystem::path fixtureDirectory = std::filesystem::path(__FILE__).parent_path() / "Fixtures";
    if (!Check(documentBuilder.Build(document, fixtureDirectory, &error), "glTF document builds"))
    {
        return false;
    }

    const Engine::Scene& scene = documentBuilder.Builder().GetScene();
    const Engine::SceneMesh& mesh = documentBuilder.Builder().GetMesh();
    const RtPbrSurvey::SceneDocumentBuildResult& result = documentBuilder.Result();
    bool passed = Check(scene.instances.size() == 7, "both glTF placements produce instances");
    passed &= Check(scene.instances[5].meshId == scene.instances[6].meshId,
                    "identical glTF assets share one converted mesh");
    passed &= Check(mesh.materials.size() == 4, "glTF source materials are retained alongside document materials");
    passed &= Check(NearlyEqual(mesh.materials[2].metallicFactor, 0.25f) &&
                        NearlyEqual(mesh.materials[2].roughnessFactor, 0.75f) &&
                        NearlyEqual(mesh.materials[3].metallicFactor, 0.5f) &&
                        NearlyEqual(mesh.materials[3].roughnessFactor, 0.3f),
                    "glTF PBR material factors are preserved");
    passed &= Check(result.nodeInstanceIndices.contains("gltf-first") &&
                        result.nodeInstanceIndices.contains("gltf-second"),
                    "each glTF node maps to its renderer instance");
    return passed;
}

bool TestNrdStableRadianceFixture()
{
    const std::filesystem::path directory = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scene/PathTracingValidation/13-nrd-stable-radiance";
    RtPbrSurvey::SceneDocument document;
    RtPbrSurvey::SceneDocumentBuilder builder;
    std::string error;
    if (!Check(RtPbrSurvey::LoadSceneDocumentFile((directory / "scene.json").string(), document, &error),
               "NRD stable-radiance scene schema is valid") ||
        !Check(builder.Build(document, directory, &error), "NRD stable-radiance glTF builds"))
    {
        std::cerr << error << '\n';
        return false;
    }
    const Engine::SceneMesh& mesh = builder.Builder().GetMesh();
    return Check(mesh.materials.size() == 3 && mesh.indices.size() == 18,
                 "NRD fixture contains three indexed panels") &&
           Check(!mesh.materials[0].unlit && mesh.materials[1].unlit && !mesh.materials[2].unlit,
                 "NRD fixture keeps only the center panel Unlit");
}

bool TestNrdDisocclusionFixture()
{
    const std::filesystem::path directory = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scene/PathTracingValidation/14-nrd-disocclusion";
    RtPbrSurvey::SceneDocument document;
    RtPbrSurvey::SceneDocumentBuilder builder;
    std::string error;
    return Check(RtPbrSurvey::LoadSceneDocumentFile((directory / "scene.json").string(), document, &error),
                 "NRD disocclusion scene schema is valid") &&
           Check(builder.Build(document, directory, &error), "NRD disocclusion geometry builds") &&
           Check(builder.Builder().GetScene().instances.size() == 2 && document.materials.size() == 2,
                 "NRD disocclusion fixture contains foreground and background");
}

bool TestThinReviewFixtures()
{
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scene/PathTracingValidation";
    for (const char* name : {"17-thin-transmission-baseline", "18-thin-transmission-angle", "19-thin-transmission-layers",
                            "20-thin-transmission-visual-comparison", "21-gltf-thin-transmission-suzanne",
                            "22-thin-transmission-textures", "23-rough-thin-transmission",
                            "24-roughness-textured-thin-transmission", "25-metallic-thin-transmission",
                            "26-alpha-mask-thin-transmission"})
    {
        RtPbrSurvey::SceneDocument document;
        RtPbrSurvey::SceneDocumentBuilder builder;
        std::string error;
        const std::filesystem::path directory = root / name;
        if (!Check(RtPbrSurvey::LoadSceneDocumentFile((directory / "scene.json").string(), document, &error),
                   "thin review document loads") ||
            !Check(builder.Build(document, directory, &error), "thin review scene and background asset build"))
        {
            std::cerr << name << ": " << error << '\n';
            return false;
        }
        bool hasThin = false;
        for (const auto& material : builder.Builder().GetMesh().materials)
        {
            hasThin |= material.thinTransmission && material.transmissionFactor > 0;
        }
        const size_t minimumInstances = std::string(name) == "21-gltf-thin-transmission-suzanne" ? 2 : 4;
        if (!Check(hasThin && !document.description.empty() &&
                       builder.Builder().GetScene().instances.size() >= minimumInstances &&
                       std::filesystem::is_regular_file(directory / document.renderPresetPath),
                   "thin fixture has active transmission, geometry, description and render preset"))
        {
            return false;
        }
    }
    return true;
}

bool TestTransmissionReachesRuntimeMaterial()
{
    RtPbrSurvey::SceneDocumentBuilder builder;
    std::string error;
    RtPbrSurvey::SceneDocument document = CreatePrimitiveDocument();
    if (!Check(builder.Build(document, &error), "opaque scene builds before transmission"))
    {
        return false;
    }
    const size_t instanceCount = builder.Builder().GetScene().instances.size();
    document.materials[0].transmission.mode = RtPbrSurvey::SceneTransmissionMode::Thin;
    document.materials[0].transmission.factor = 0.5f;
    document.materials[0].transmission.tint = {0.2f, 0.4f, 0.6f};
    document.materials[0].transmission.ior = 1.33f;
    if (!Check(builder.Build(document, &error), "thin transmission scene builds"))
    {
        return false;
    }
    const auto& material = builder.Builder().GetMesh().materials[builder.Result().materialIds.at("metal")];
    const bool passed = Check(material.thinTransmission && NearlyEqual(material.transmissionFactor, 0.5f) &&
                     NearlyEqual(material.transmissionTint.x, 0.2f) && NearlyEqual(material.transmissionTint.y, 0.4f) &&
                     NearlyEqual(material.transmissionTint.z, 0.6f) && NearlyEqual(material.transmissionIor, 1.33f),
                 "thin transmission data reaches runtime material") &&
           Check(builder.Builder().GetScene().instances.size() == instanceCount, "transmission preserves geometry");
    document.materials[0].transmission.mode = RtPbrSurvey::SceneTransmissionMode::Volume;
    return passed && Check(!builder.Build(document, &error) && error.find("not connected") != std::string::npos,
        "volume data is not silently rendered as opaque or thin") &&
        Check(builder.Builder().GetScene().instances.size() == instanceCount && material.thinTransmission,
            "unsupported volume keeps the published thin scene intact");
}

bool TestQualifiedVolumeBuild()
{
    using namespace RtPbrSurvey;
    SceneDocument document = CreatePrimitiveDocument();
    document.nodes.resize(1);
    document.materials.resize(1);
    document.materials[0].metallic = 0;
    document.materials[0].roughness = 0;
    document.materials[0].baseColor = {1, 1, 1, 1};
    document.materials[0].transmission.mode = SceneTransmissionMode::Volume;
    document.materials[0].transmission.factor = 1;
    SceneDocumentBuilder builder;
    std::string error;
    bool passed = Check(builder.Build(document, &error), "clear smooth volume cube builds");
    if (!passed)
    {
        return false;
    }
    const auto& material = builder.Builder().GetMesh().materials[builder.Result().materialIds.at("metal")];
    passed &= Check(material.volumeTransmission && !material.thinTransmission && material.transmissionIor == 1.5f,
        "volume is distinct from thin in runtime data");
    SceneDocument invalid = document;
    invalid.nodes.push_back(invalid.nodes[0]);
    invalid.nodes.back().id = "second";
    passed &= Check(!builder.Build(invalid, &error), "multiple volume instances rejected");
    invalid = document;
    invalid.camera.position = invalid.nodes[0].transform.translation;
    passed &= Check(!builder.Build(invalid, &error), "camera inside volume rejected");
    invalid = document;
    invalid.nodes[0].primitive.kind = ScenePrimitiveKind::Plane;
    passed &= Check(!builder.Build(invalid, &error), "open volume plane rejected");
    invalid = document;
    invalid.materials[0].transmission.volume.attenuationColor = {0.2f, 0.5f, 1};
    invalid.materials[0].transmission.volume.attenuationDistance = 1;
    passed &= Check(builder.Build(invalid, &error), "colored volume absorption builds");
    if (passed)
    {
        const auto& absorption = builder.Builder().GetMesh().materials[builder.Result().materialIds.at("metal")].volumeAbsorption;
        passed &= Check(NearlyEqual(absorption.attenuationColor[0], 0.2f) && absorption.attenuationColor[1] == 0.5f &&
            absorption.attenuationColor[2] == 1 && absorption.attenuationDistance == 1,
            "absorption values reach runtime material data");
    }
    document.nodes[0].primitive.kind = ScenePrimitiveKind::Sphere;
    passed &= Check(builder.Build(document, &error), "clear smooth volume sphere builds");
    document.materials[0].roughness = 0.35f;
    passed &= Check(builder.Build(document, &error), "rough volume sphere builds");
    if (passed)
    {
        passed &= Check(NearlyEqual(builder.Builder().GetMesh().materials[builder.Result().materialIds.at("metal")].roughnessFactor,
            0.35f), "rough volume preserves runtime roughness");
    }
    document.nodes[0].primitive.kind = ScenePrimitiveKind::Cube;
    passed &= Check(builder.Build(document, &error), "rough volume cube builds");
    for (const char* name : {"33-rough-volume-cube", "34-rough-volume-sphere", "35-gltf-rough-volume-tetra"})
    {
        const std::filesystem::path directory = std::filesystem::path(__FILE__).parent_path().parent_path() /
            "Assets/Scene/PathTracingValidation" / name;
        SceneDocument saved;
        passed &= Check(LoadSceneDocumentFile((directory / "scene.json").string(), saved, &error),
            "rough volume saved scene schema loads");
        if (!passed)
        {
            std::cerr << error << '\n';
            return false;
        }
        passed &= Check(builder.Build(saved, directory, &error), "rough volume saved scene builds");
    }
    return passed;
}

bool TestGltfFailureKeepsPublishedScene()
{
    RtPbrSurvey::SceneDocumentBuilder documentBuilder;
    std::string error;
    const RtPbrSurvey::SceneDocument validDocument = CreatePrimitiveDocument();
    if (!Check(documentBuilder.Build(validDocument, &error), "baseline primitive scene builds"))
    {
        return false;
    }

    RtPbrSurvey::SceneDocument invalidDocument = CreatePrimitiveDocument();
    invalidDocument.assets.push_back({"missing", "Gltf/missing.gltf"});
    RtPbrSurvey::SceneNode gltf;
    gltf.id = "missing-gltf";
    gltf.type = RtPbrSurvey::SceneNodeType::Gltf;
    gltf.assetId = "missing";
    invalidDocument.nodes.push_back(gltf);
    const std::filesystem::path fixtureDirectory = std::filesystem::path(__FILE__).parent_path() / "Fixtures";

    return Check(!documentBuilder.Build(invalidDocument, fixtureDirectory, &error), "missing glTF is rejected") &&
           Check(!error.empty(), "missing glTF reports a diagnostic") &&
           Check(documentBuilder.Builder().GetScene().instances.size() == 5,
                 "failed glTF build retains the previously published scene");
}

} // namespace

bool TestRuntimeTurntable()
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scene/PathTracingValidation/35-gltf-rough-volume-tetra/scene.json";
    Engine::SceneDocumentRuntimeScene scene(path);
    std::string error;
    if (!Check(scene.LoadFromFile(&error), "turntable saved scene loads"))
    {
        return false;
    }
    const auto same = [](const DirectX::XMFLOAT4X4& a, const DirectX::XMFLOAT4X4& b)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                if (std::abs(a.m[row][col] - b.m[row][col]) > 0.00001f)
                {
                    return false;
                }
            }
        }
        return true;
    };
    const auto original = scene.GetScene().instances[0].world;
    Engine::SampleSceneUpdateContext context;
    scene.Update(1, context);
    bool passed = Check(same(original, scene.GetScene().instances[0].world), "paused turntable stays fixed");
    context.isPlaying = true;
    scene.Update(6, context);
    const auto rotated = scene.GetScene().instances[0].world;
    passed &= Check(!same(original, rotated) && same(original, scene.GetScene().instances[0].prevWorld),
        "playing turntable rotates and preserves previous transform");
    context.isPlaying = false;
    scene.Update(6, context);
    passed &= Check(same(rotated, scene.GetScene().instances[0].world) &&
        same(rotated, scene.GetScene().instances[0].prevWorld), "pause clears motion without resetting angle");
    scene.Reset();
    passed &= Check(same(original, scene.GetScene().instances[0].world), "reset restores original transform");
    context.isPlaying = true;
    for (int step = 0; step < 24; ++step)
    {
        scene.Update(1, context);
        const auto guard = RtPbrSurvey::BuildImportedVolumeSceneGuard(scene.GetScene(), error);
        passed &= Check(guard.has_value(), "full turn preserves isolated convex volume placement");
    }
    auto document = scene.Document();
    std::string json;
    RtPbrSurvey::SceneDocument restored;
    passed &= Check(RtPbrSurvey::SerializeSceneDocument(document, json, &error) &&
        RtPbrSurvey::DeserializeSceneDocument(json, restored, &error) && restored.turntable &&
        restored.turntable->nodeId == "solid" && restored.turntable->degreesPerSecond == 15,
        "turntable settings survive serialization");
    document.turntable->nodeId = "missing";
    passed &= Check(!RtPbrSurvey::SerializeSceneDocument(document, json, &error), "missing animation target rejected");
    return passed;
}

bool TestRuntimeTranslation()
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scenes/RrObjectTranslation/scene.json";
    Engine::SceneDocumentRuntimeScene scene(path);
    std::string error;
    if (!Check(scene.LoadFromFile(&error), "RR translation scene loads"))
    {
        return false;
    }
    size_t index = 0;
    for (; index < scene.GetScene().instances.size(); ++index)
    {
        if (scene.FindNodeIdByInstanceIndex(index) == std::optional<std::string>("helmet"))
        {
            break;
        }
    }
    if (!Check(index < scene.GetScene().instances.size(), "translation target instance found"))
    {
        return false;
    }
    const auto original = scene.GetScene().instances[index].world;
    Engine::SampleSceneUpdateContext context;
    context.isPlaying = true;
    scene.Update(1.5f, context);
    const auto moved = scene.GetScene().instances[index].world;
    bool passed = Check(std::abs(moved._14 - original._14 - 1.4f) < 0.0001f &&
        std::abs(moved._11 - original._11) < 0.0001f &&
        scene.GetScene().instances[index].prevWorld._14 == original._14,
        "quarter-period translation reaches amplitude without rotation and retains previous pose");
    context.isPlaying = false;
    scene.Update(1.0f, context);
    passed &= Check(scene.GetScene().instances[index].world._14 == moved._14 &&
        scene.GetScene().instances[index].prevWorld._14 == moved._14,
        "translation pause freezes pose and clears previous-pose motion");
    scene.Reset();
    context.isPlaying = true;
    scene.Update(1.5f, context);
    passed &= Check(std::abs(scene.GetScene().instances[index].world._14 - moved._14) < 0.0001f,
        "reset restarts translation phase");
    auto document = scene.Document();
    std::string json;
    RtPbrSurvey::SceneDocument restored;
    passed &= Check(RtPbrSurvey::SerializeSceneDocument(document, json, &error) &&
        RtPbrSurvey::DeserializeSceneDocument(json, restored, &error) && restored.turntable &&
        restored.turntable->translationAmplitude.x == 1.4f &&
        restored.turntable->translationPeriodSeconds == 6.0f,
        "translation parameters survive serialization");
    document.turntable->translationPeriodSeconds = 0.0f;
    passed &= Check(!RtPbrSurvey::SerializeSceneDocument(document, json, &error),
        "zero translation period rejected");
    return passed;
}

int main()
{
    if (!TestRuntimeTranslation() || !TestRuntimeTurntable() || !TestQualifiedVolumeBuild() || !TestThinReviewFixtures() || !TestTransmissionReachesRuntimeMaterial() ||
        !TestPrimitiveBuildAndMeshReuse() || !TestGltfBuildAndMeshReuse() || !TestGltfFailureKeepsPublishedScene() ||
        !TestNrdStableRadianceFixture() || !TestNrdDisocclusionFixture())
    {
        return 1;
    }
    std::cout << "SceneDocumentBuilder tests passed.\n";
    return 0;
}
