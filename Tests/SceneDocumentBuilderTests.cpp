#include "Scene/SceneDocumentBuilder.h"

#include <DirectXMath.h>

#include <cmath>
#include <filesystem>
#include <iostream>

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
    passed &= Check(mesh.ranges.size() == 4, "identical primitive parameters reuse a mesh range");
    passed &= Check(scene.instances[0].meshId == scene.instances[1].meshId, "duplicate cubes share a mesh ID");
    passed &= Check(result.nodeInstanceIndices.size() == 5 && result.materialIds.size() == 2,
                    "node and material IDs map to renderer indices");
    passed &= Check(mesh.materials[result.materialIds.at("metal")].metallicFactor == 1.0f &&
                        NearlyEqual(mesh.materials[result.materialIds.at("metal")].roughnessFactor, 0.2f),
                    "PBR material factors are preserved");
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

int main()
{
    if (!TestPrimitiveBuildAndMeshReuse() || !TestGltfBuildAndMeshReuse() || !TestGltfFailureKeepsPublishedScene())
    {
        return 1;
    }
    std::cout << "SceneDocumentBuilder tests passed.\n";
    return 0;
}
