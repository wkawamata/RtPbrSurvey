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
    Require(names.size() == 6, "Only reachable named mesh nodes should be enumerated.");
    Require(names[0] == "PartA" && names[1] == "PartB", "Mesh node names should preserve scene traversal order.");

    Engine::SceneBuilder builder;
    const Engine::GltfNodeMeshAddResult missing = builder.AddGltfNodeMesh(loadResult.asset, "Missing");
    Require(missing.status == Engine::GltfNodeMeshStatus::NodeNotFound, "Missing node names should fail explicitly.");
    Require(!missing.meshId, "A missing node must not return a mesh ID.");

    const Engine::GltfNodeMeshAddResult duplicate = builder.AddGltfNodeMesh(loadResult.asset, "Repeated");
    Require(duplicate.status == Engine::GltfNodeMeshStatus::DuplicateNodeName,
            "Duplicate node names should fail explicitly.");
    Require(builder.GetMesh().ranges.empty(), "Failed node lookup must not mutate the SceneBuilder.");

    const Engine::GltfSceneAsset invalidAsset;
    const Engine::GltfNodeMeshAddResult invalid = builder.AddGltfNodeMesh(invalidAsset, "PartA");
    Require(invalid.status == Engine::GltfNodeMeshStatus::InvalidAsset, "An invalid CPU asset should fail explicitly.");
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
    Require(builder.GetMesh().vertices.size() == 18,
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
        TestExistingFlattenedMeshContract();
    }
    catch (const std::exception& error)
    {
        std::cerr << "glTF node mesh tests failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
