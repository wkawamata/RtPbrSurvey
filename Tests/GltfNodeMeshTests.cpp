#include "GltfLoader.h"
#include "Scene/SceneBuilder.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

std::filesystem::path FixturePath()
{
    return std::filesystem::path(__FILE__).parent_path() / "Fixtures" / "Gltf" / "multi-node.gltf";
}

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void TestNodeEnumerationAndFailures()
{
    const Engine::GltfSceneAssetLoadResult loadResult = Engine::LoadGltfSceneAsset(FixturePath().string());
    Require(static_cast<bool>(loadResult), "The multi-node glTF fixture should load.");

    const std::vector<std::string> names = Engine::GetGltfMeshNodeNames(loadResult.asset);
    Require(names.size() == 7, "Only reachable named mesh nodes should be enumerated.");
    Require(names[0] == "PartA" && names[1] == "PartB", "Mesh node names should preserve scene traversal order.");

    Engine::SceneBuilder builder;
    const Engine::GltfNodeMeshAddResult missing = builder.AddGltfNodeMesh(loadResult.asset, "Missing");
    Require(missing.status == Engine::GltfNodeMeshStatus::NodeNotFound, "Missing node names should fail explicitly.");
    Require(!missing.meshId, "A missing node must not return a mesh ID.");

    const Engine::GltfNodeMeshAddResult duplicate = builder.AddGltfNodeMesh(loadResult.asset, "Repeated");
    Require(duplicate.status == Engine::GltfNodeMeshStatus::DuplicateNodeName,
            "Duplicate node names should fail explicitly.");
    Require(builder.GetMesh().ranges.empty(), "Failed node lookup must not mutate the SceneBuilder.");

    const Engine::GltfNodeMeshAddResult repeatedByIndex = builder.AddGltfNodeMesh(loadResult.asset, 6);
    Require(static_cast<bool>(repeatedByIndex), "A mesh node should be addable by its glTF node index.");
    Require(repeatedByIndex.meshId.has_value(), "An indexed node mesh should return a mesh ID.");

    const Engine::GltfNodeMeshAddResult missingByIndex = builder.AddGltfNodeMesh(loadResult.asset, 99);
    Require(missingByIndex.status == Engine::GltfNodeMeshStatus::NodeNotFound,
            "An invalid glTF node index should fail explicitly.");

    const Engine::GltfSceneAsset invalidAsset;
    const Engine::GltfNodeMeshAddResult invalid = builder.AddGltfNodeMesh(invalidAsset, "PartA");
    Require(invalid.status == Engine::GltfNodeMeshStatus::InvalidAsset, "An invalid CPU asset should fail explicitly.");
}

void TestGltfRotationAndHandednessConversion()
{
    const Engine::GltfSceneAssetLoadResult loadResult = Engine::LoadGltfSceneAsset(FixturePath().string());
    Require(static_cast<bool>(loadResult), "The multi-node glTF fixture should load.");

    Engine::SceneBuilder builder;
    const Engine::GltfNodeMeshAddResult rotated = builder.AddGltfNodeMesh(loadResult.asset, 8);
    Require(static_cast<bool>(rotated), "The rotated mesh node should be addable by index.");

    const Engine::SceneMesh::Range& range = builder.GetMesh().ranges.front();
    const Engine::SceneVertex& origin = builder.GetMesh().vertices[range.firstVertex];
    const Engine::SceneVertex& xVertex = builder.GetMesh().vertices[range.firstVertex + 1];
    Require(NearlyEqual(origin.position.x, 0.0f) && NearlyEqual(origin.position.z, -2.0f),
            "glTF translation should be converted from RH to the engine LH convention.");
    Require(NearlyEqual(xVertex.position.x, 0.0f) && NearlyEqual(xVertex.position.z, -1.0f),
            "glTF quaternion rotation should be applied before the handedness conversion.");
}

void TestSurfaceTransforms()
{
    using namespace DirectX;
    const std::filesystem::path path = FixturePath().parent_path() / "surface-transforms.gltf";
    const Engine::GltfSceneAssetLoadResult loadResult = Engine::LoadGltfSceneAsset(path.string());
    Require(static_cast<bool>(loadResult), "The surface-transform fixture should load.");
    const auto extract = [&](const char* name)
    {
        Engine::SceneBuilder builder;
        Require(static_cast<bool>(builder.AddGltfNodeMesh(loadResult.asset, name)),
                "Each transformed surface should extract.");
        return builder.GetMesh();
    };
    const Engine::SceneMesh scaled = extract("Scaled");
    const Engine::SceneMesh matrix = extract("Matrix");
    for (size_t i = 0; i < scaled.vertices.size(); ++i)
    {
        const Engine::SceneVertex& a = scaled.vertices[i];
        const Engine::SceneVertex& b = matrix.vertices[i];
        Require(NearlyEqual(a.position.x, b.position.x) && NearlyEqual(a.position.y, b.position.y) &&
                    NearlyEqual(a.position.z, b.position.z), "Matrix and TRS positions must agree.");
        Require(NearlyEqual(a.normal.x, b.normal.x) && NearlyEqual(a.normal.y, b.normal.y) &&
                    NearlyEqual(a.normal.z, b.normal.z), "Matrix and TRS normals must agree.");
        Require(NearlyEqual(a.tangent.x, b.tangent.x) && NearlyEqual(a.tangent.y, b.tangent.y) &&
                    NearlyEqual(a.tangent.z, b.tangent.z), "Matrix and TRS tangents must agree.");
    }
    for (const char* name : {"Scaled", "Mirrored"})
    {
        const Engine::SceneMesh mesh = extract(name);
        const Engine::SceneVertex& vertex = mesh.vertices[0];
        const XMVECTOR normal = XMLoadFloat3(&vertex.normal);
        const XMVECTOR tangent = XMVectorSet(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f);
        Require(NearlyEqual(XMVectorGetX(XMVector3Dot(normal, tangent)), 0.0f),
                "Nonuniform scale must preserve perpendicular surface normal and tangent.");
        Require(NearlyEqual(XMVectorGetX(XMVector3Length(normal)), 1.0f) &&
                    NearlyEqual(XMVectorGetX(XMVector3Length(tangent)), 1.0f), "Surface directions must be normalized.");
        const XMVECTOR p0 = XMLoadFloat3(&mesh.vertices[mesh.indices[0]].position);
        const XMVECTOR p1 = XMLoadFloat3(&mesh.vertices[mesh.indices[1]].position);
        const XMVECTOR p2 = XMLoadFloat3(&mesh.vertices[mesh.indices[2]].position);
        const XMVECTOR geometricNormal = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
        Require(XMVectorGetX(XMVector3Dot(geometricNormal, normal)) > 0.9999f,
                "Mirroring and LH conversion must preserve front-face winding relative to normals.");
        Require(NearlyEqual(vertex.tangent.w, std::string(name) == "Mirrored" ? 1.0f : -1.0f),
                "Tangent handedness must include both node mirroring and RH-to-LH conversion.");
        const float sign = std::string(name) == "Mirrored" ? -1.0f : 1.0f;
        const XMVECTOR expectedTangent = XMVector3Normalize(XMVectorSet(1.0f, 2.0f * sign, 0.0f, 0.0f));
        Require(XMVectorGetX(XMVector3Dot(tangent, expectedTangent)) > 0.9999f,
                "Tangents must use the forward transform, not the normal inverse transpose.");
    }
}

void TestIndependentNodeMeshesAndLifetime()
{
    Engine::SceneBuilder builder;
    {
        const Engine::GltfSceneAssetLoadResult loadResult = Engine::LoadGltfSceneAsset(FixturePath().string());
        Require(static_cast<bool>(loadResult), "The multi-node glTF fixture should load.");

        const Engine::GltfNodeMeshAddResult partA = builder.AddGltfNodeMesh(loadResult.asset, "PartA");
        const Engine::GltfNodeMeshAddResult partB = builder.AddGltfNodeMesh(loadResult.asset, "PartB");
        const Engine::GltfNodeMeshAddResult parent = builder.AddGltfNodeMesh(loadResult.asset, "ParentPart");
        Require(partA && partB && parent, "Each unique named mesh node should be independently addable.");
        Require(*partA.meshId != *partB.meshId && *partB.meshId != *parent.meshId,
                "Each named node should receive a separate SceneMeshId.");
    }

    const Engine::SceneMesh& mesh = builder.GetMesh();
    Require(mesh.ranges.size() == 3, "Builder data should remain valid after releasing the CPU glTF asset.");
    Require(mesh.vertices.size() == 9, "Extracting a parent node must not implicitly include its child mesh.");
    Require(mesh.materials.size() == 6, "Each node addition should deep-copy and globally remap materials.");
    Require(mesh.textures.size() == 3, "Each node addition should deep-copy and globally remap textures.");

    const Engine::SceneMesh::Range& partARange = mesh.ranges[0];
    const Engine::SceneMesh::Range& partBRange = mesh.ranges[1];
    const Engine::SceneVertex& partAOrigin = mesh.vertices[partARange.firstVertex];
    const Engine::SceneVertex& partBOrigin = mesh.vertices[partBRange.firstVertex];
    Require(NearlyEqual(partAOrigin.position.x, 11.0f), "The selected node and ancestor X transforms should be baked.");
    Require(NearlyEqual(partBOrigin.position.x, 10.0f) && NearlyEqual(partBOrigin.position.y, 2.0f) &&
                NearlyEqual(partBOrigin.position.z, -3.0f),
            "Ancestor transforms should be baked using the renderer LH convention.");
    Require(partAOrigin.materialId == 0, "The first node material should use the first global material range.");
    Require(partBOrigin.materialId == 3, "The second node material should be remapped to its global material range.");
    Require(mesh.materials[0].albedoTexIndex == 0, "The first node texture should use the first global texture range.");
    Require(mesh.materials[2].albedoTexIndex == 1, "The second node texture should be globally remapped.");
}

void TestExistingFlattenedMeshContract()
{
    Engine::SceneBuilder builder;
    const std::optional<Engine::SceneMeshId> meshId = builder.AddGltfMesh(FixturePath().string());
    Require(meshId.has_value(), "The existing AddGltfMesh(path) API should remain usable.");
    Require(builder.GetMesh().ranges.size() == 1, "The existing API should still produce one flattened mesh range.");
    Require(builder.GetMesh().vertices.size() == 21,
            "The existing API should still flatten every default-scene mesh node.");
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        if (argc == 2)
        {
            const Engine::GltfSceneAssetLoadResult loadResult = Engine::LoadGltfSceneAsset(argv[1]);
            Require(static_cast<bool>(loadResult), "The requested glTF asset should load.");
            Engine::SceneBuilder builder;
            for (const std::string& name : Engine::GetGltfMeshNodeNames(loadResult.asset))
            {
                const Engine::GltfNodeMeshAddResult addResult = builder.AddGltfNodeMesh(loadResult.asset, name);
                Require(static_cast<bool>(addResult), "Each named mesh node should be independently addable.");
                std::cout << name << '=' << *addResult.meshId << '\n';
            }
            return 0;
        }

        TestNodeEnumerationAndFailures();
        TestIndependentNodeMeshesAndLifetime();
        TestGltfRotationAndHandednessConversion();
        TestSurfaceTransforms();
        TestExistingFlattenedMeshContract();
    }
    catch (const std::exception& error)
    {
        std::cerr << "glTF node mesh tests failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
