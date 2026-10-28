#include "GltfLoader.h"
#include "Scene/SceneBuilder.h"
#include "Scene/VolumeMeshTopology.h"
#include "Scene/ConvexVolumeBounds.h"
#include "Scene/ImportedVolumeScene.h"
#include "Scene/SceneDocumentBuilder.h"
#include "Scene/SceneDocumentJson.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <limits>

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

void TestVolumeMeshTopology()
{
    using Position = std::array<float, 3>;
    const std::vector<Position> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::vector<uint32_t> indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    const auto validate = RtPbrSurvey::ValidateVolumeMeshTopology;
    Require(validate(positions, indices).empty(), "Outward closed tetrahedron must pass topology validation.");
    std::vector<Position> seams;
    std::vector<uint32_t> seamIndices;
    for (uint32_t index : indices)
    {
        seamIndices.push_back(static_cast<uint32_t>(seams.size()));
        seams.push_back(positions[index]);
    }
    Require(validate(seams, seamIndices).empty(), "Exact-position UV/normal seams must weld for topology only.");
    auto transformed = positions;
    for (auto& position : transformed)
    {
        for (float& value : position)
        {
            value = value * 0.001f + 10.0f;
        }
    }
    Require(validate(transformed, indices).empty(), "Small translated closed meshes must not use a fixed volume threshold.");
    auto invalid = indices;
    invalid.resize(9);
    Require(!validate(positions, invalid).empty(), "Open volume must be rejected.");
    invalid = indices;
    std::swap(invalid[0], invalid[1]);
    Require(!validate(positions, invalid).empty(), "Mixed face winding must be rejected.");
    invalid = indices;
    for (size_t face = 0; face < invalid.size(); face += 3)
    {
        std::swap(invalid[face], invalid[face + 1]);
    }
    Require(!validate(positions, invalid).empty(), "Inward closed shells must be rejected.");
    invalid = indices;
    invalid.insert(invalid.end(), {0, 2, 1});
    Require(!validate(positions, invalid).empty(), "Three faces sharing an edge must be rejected.");
    invalid = indices;
    invalid[0] = 99;
    Require(!validate(positions, invalid).empty(), "Invalid indices must be rejected.");
    invalid = indices;
    invalid[0] = invalid[1];
    Require(!validate(positions, invalid).empty(), "Zero-area triangles must be rejected.");
    invalid = indices;
    invalid.pop_back();
    Require(!validate(positions, invalid).empty(), "Partial triangles must be rejected.");
    transformed = positions;
    transformed[0][0] = std::numeric_limits<float>::quiet_NaN();
    Require(!validate(transformed, indices).empty(), "NaN positions must be rejected before welding.");
    transformed[0][0] = std::numeric_limits<float>::infinity();
    Require(!validate(transformed, indices).empty(), "Infinite positions must be rejected.");
    transformed = positions;
    for (const auto& position : positions)
    {
        transformed.push_back({position[0] + 2, position[1], position[2]});
    }
    invalid = indices;
    for (uint32_t index : indices)
    {
        invalid.push_back(index + 4);
    }
    Require(!validate(transformed, invalid).empty(), "Separate shells must not be treated as one medium.");
    transformed = positions;
    for (const auto& position : positions)
    {
        transformed.push_back({-position[0], -position[1], -position[2]});
    }
    invalid = indices;
    for (size_t face = 0; face < indices.size(); face += 3)
    {
        invalid.insert(invalid.end(), {indices[face + 1] + 4, indices[face] + 4, indices[face + 2] + 4});
    }
    Require(!validate(transformed, invalid).empty(), "Shells touching at a single vertex must be rejected.");
}

void TestConvexVolumeBounds()
{
    using Bounds = RtPbrSurvey::ConvexVolumeBounds;
    using Location = RtPbrSurvey::VolumePointLocation;
    const std::vector<Bounds::Position> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::vector<uint32_t> indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    std::string error = "stale";
    const auto bounds = Bounds::Build(positions, indices, &error);
    Require(bounds.has_value() && error.empty(), "Convex outward tetrahedron must qualify and clear stale errors.");
    Require(bounds->ClassifyPoint({0.1f, 0.1f, 0.1f}) == Location::Inside, "Interior camera must be detected.");
    Require(bounds->ClassifyPoint({1, 1, 1}) == Location::Outside, "Exterior camera must be detected.");
    Require(bounds->ClassifyPoint({0, 0.2f, 0.2f}) == Location::Boundary, "Camera on a face must be rejected as boundary.");
    Require(bounds->ClassifyPoint({0, 0, 0}) == Location::Boundary, "Camera on a vertex must be boundary.");
    Require(bounds->ClassifyPoint({-0.0000005f, 0.2f, 0.2f}) == Location::Boundary,
        "Near-surface camera must remain in the conservative boundary band.");
    Require(bounds->ClassifyPoint({std::numeric_limits<float>::quiet_NaN(), 0, 0}) == Location::Invalid,
        "Nonfinite camera cannot pass the outside check.");
    Require(!bounds->MayOverlapBounds({2, 2, 2}, {3, 3, 3}), "Clearly separated bounds must pass placement gate.");
    Require(bounds->MayOverlapBounds({1, 0, 0}, {2, 1, 1}), "Touching bounds must fail closed.");
    Require(bounds->MayOverlapBounds({0.8f, 0.8f, 0.8f}, {0.9f, 0.9f, 0.9f}),
        "AABB overlap must remain conservative even outside the actual tetrahedron.");
    Require(bounds->MayOverlapBounds({2, 0, 0}, {1, 1, 1}), "Inverted bounds must fail closed.");
    Require(bounds->MayOverlapBounds({2, 0, 0}, {3, std::numeric_limits<float>::infinity(), 1}),
        "Nonfinite bounds must fail closed even with another separating axis.");
    auto transformed = positions;
    for (auto& position : transformed)
    {
        for (float& value : position)
        {
            value = 10.0f + value * 0.001f;
        }
    }
    const auto small = Bounds::Build(transformed, indices, &error);
    Require(small.has_value() && small->ClassifyPoint({10.00025f, 10.00025f, 10.00025f}) == Location::Inside &&
        small->ClassifyPoint({9.99f, 10, 10}) == Location::Outside,
        "Classification must work after uniform scale and translation.");
    transformed = positions;
    transformed[3][2] = 0.00000001f;
    Require(!Bounds::Build(transformed, indices, &error) && !error.empty(),
        "Numerically thin solids must not qualify for camera classification.");
    const std::vector<Bounds::Position> concave = {{0, 0, -0.2f}, {0, 0, -1}, {1, 0, 0},
        {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
    const std::vector<uint32_t> octahedron = {0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2,
        1, 3, 2, 1, 4, 3, 1, 5, 4, 1, 2, 5};
    Require(RtPbrSurvey::ValidateVolumeMeshTopology(concave, octahedron).empty(),
        "Concave fixture must pass topology so convexity is tested independently.");
    Require(!Bounds::Build(concave, octahedron, &error) && error.find("convex") != std::string::npos,
        "Closed concave shell must be rejected by convexity gate.");
    auto open = indices;
    open.resize(9);
    Require(!Bounds::Build(positions, open), "Convex qualification must not bypass the topology gate.");
}

void TestSolidVolumeImport()
{
    using Json = nlohmann::json;
    const auto path = std::filesystem::temp_directory_path() /
        ("RtPbrSurvey-solid-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gltf");
    const auto binary = std::filesystem::path(path).replace_extension(".bin");
    struct Cleanup
    {
        std::filesystem::path json, bin;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove(json, ignored);
            std::filesystem::remove(bin, ignored);
        }
    } cleanup{path, binary};
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    const uint16_t indices[] = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    {
        std::ofstream output(binary, std::ios::binary);
        output.write(reinterpret_cast<const char*>(positions), sizeof(positions));
        output.write(reinterpret_cast<const char*>(indices), sizeof(indices));
    }
    Json document = {
        {"asset", {{"version", "2.0"}}}, {"scene", 0}, {"scenes", {{{"nodes", {0}}}}},
        {"nodes", {{{"name", "Solid"}, {"mesh", 0}}}},
        {"buffers", {{{"uri", binary.filename().string()}, {"byteLength", 72}}}},
        {"bufferViews", {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}},
                          {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 24}}}},
        {"accessors", {{{"bufferView", 0}, {"componentType", 5126}, {"count", 4}, {"type", "VEC3"}},
                       {{"bufferView", 1}, {"componentType", 5123}, {"count", 12}, {"type", "SCALAR"}}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", 0}}}, {"indices", 1}, {"material", 0}}}}}}},
        {"extensionsUsed", {"KHR_materials_transmission", "KHR_materials_volume"}},
        {"extensionsRequired", {"KHR_materials_transmission", "KHR_materials_volume"}},
        {"materials", {{{"pbrMetallicRoughness", {{"metallicFactor", 0}, {"roughnessFactor", 0}}},
            {"extensions", {{"KHR_materials_transmission", {{"transmissionFactor", 1}}},
                {"KHR_materials_volume", {{"thicknessFactor", 1}, {"attenuationColor", {0.25, 0.5, 0.8}},
                    {"attenuationDistance", 2}}}}}}}}};
    const auto load = [&]()
    {
        std::ofstream output(path, std::ios::binary);
        output << document.dump(2);
        output.close();
        return Engine::LoadGltfSceneAsset(path.string());
    };
    const auto loaded = load();
    Require(static_cast<bool>(loaded), "Single convex volume asset must load.");
    Engine::SceneBuilder builder;
    const auto mesh = builder.AddGltfNodeMesh(loaded.asset, "Solid");
    Require(static_cast<bool>(mesh), "Solid mesh node must convert.");
    const auto& material = builder.GetMesh().materials[0];
    Require(material.volumeTransmission && material.importedVolumeTransmission && !material.thinTransmission &&
        material.volumeAbsorption.attenuationColor[0] == 0.25f && material.volumeAbsorption.attenuationDistance == 2.0f,
        "Solid medium identity and absorption must survive renderer material conversion.");
    builder.AddInstance(*mesh.meshId, DirectX::XMMatrixIdentity(), 0);
    std::string error;
    auto guard = RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error);
    Require(guard.has_value() && error.empty(), "Single imported instance must qualify.");
    Require(guard->ClassifyPoint({0.1f, 0.1f, -0.1f}) == RtPbrSurvey::VolumePointLocation::Inside,
        "Camera guard must use converted engine coordinates.");
    builder.AddInstance(*mesh.meshId, DirectX::XMMatrixTranslation(3, 0, 0), 0);
    Require(!RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error) && !error.empty(),
        "Second volume instance must fail even with the same material.");
    builder.GetScene().instances.pop_back();
    builder.GetScene().instances[0].world._11 = -1;
    Require(!RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error),
        "Mirrored scene placement must fail outward-winding qualification.");
    builder.GetScene().instances[0].world._11 = 1;
    DirectX::XMStoreFloat4x4(&builder.GetScene().instances[0].world,
        DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(2, 2, 2) * DirectX::XMMatrixTranslation(3, 4, 5)));
    guard = RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error);
    Require(guard && guard->ClassifyPoint({3.2f, 4.2f, 4.8f}) == RtPbrSurvey::VolumePointLocation::Inside &&
        guard->ClassifyPoint({0.1f, 0.1f, -0.1f}) == RtPbrSurvey::VolumePointLocation::Outside,
        "Instance storage transpose, scale and translation must be applied before camera checks.");
    const auto opaqueMaterial = builder.AddMaterial({});
    const auto cube = builder.AddCube(0.2f);
    builder.AddInstance(cube, DirectX::XMMatrixTranslation(3.2f, 4.2f, 4.8f), opaqueMaterial);
    Require(!RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error) && !error.empty(),
        "Opaque geometry inside imported volume must fail placement guard.");
    builder.GetScene().instances.pop_back();
    builder.AddInstance(cube, DirectX::XMMatrixTranslation(10, 0, 0), opaqueMaterial);
    Require(RtPbrSurvey::BuildImportedVolumeSceneGuard(builder.GetScene(), error).has_value(),
        "Separated opaque geometry must be allowed.");
    Engine::SceneBuilder legacy;
    Require(legacy.LoadGltfMesh(path.string()) && legacy.GetMesh().materials[0].volumeTransmission,
        "Legacy importer must preserve solid metadata too.");
    document["meshes"][0]["primitives"].push_back(document["meshes"][0]["primitives"][0]);
    Require(!load(), "Multiple primitives must not be silently joined into one medium.");
    document["meshes"][0]["primitives"].erase(1);
    document["accessors"][1]["count"] = 9;
    Require(!load(), "Open imported solid must be rejected before scene publication.");
    const auto scenePath = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Assets/Scene/PathTracingValidation/31-gltf-volume-tetra/scene.json";
    RtPbrSurvey::SceneDocument saved;
    Require(RtPbrSurvey::LoadSceneDocumentFile(scenePath.string(), saved, &error), "Saved glTF volume scene must load.");
    RtPbrSurvey::SceneDocumentBuilder sceneBuilder;
    Require(sceneBuilder.Build(saved, scenePath.parent_path(), &error), "Saved glTF volume scene must build.");
    saved.camera.position = {-0.3f, -0.3f, 0.3f};
    Require(!sceneBuilder.Build(saved, scenePath.parent_path(), &error) && error.find("Camera") != std::string::npos,
        "Scene builder must reject a camera inside the imported solid.");
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
    const Engine::SceneMaterial& material = scaled.materials[0];
    Require(NearlyEqual(material.baseColorFactor.x, 0.2f) && NearlyEqual(material.baseColorFactor.w, 0.8f),
            "Imported base-color factors must survive SceneBuilder conversion.");
    Require(NearlyEqual(material.emissiveFactor.x, 0.1f) && NearlyEqual(material.emissiveFactor.z, 0.9f),
            "Imported emissive factors must survive SceneBuilder conversion.");
    Require(NearlyEqual(material.normalTextureScale, 0.35f), "Imported normal scale must survive conversion.");
    Require(material.emissiveTexIndex >= 0, "Factor-only emission must receive a neutral white texture.");
    const Engine::SceneTexture& emission = scaled.textures[material.emissiveTexIndex];
    Require(emission.pixels == std::vector<unsigned char>({255, 255, 255, 255}),
            "Factor-only emission must not use the black missing-emission fallback.");
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
    Require(NearlyEqual(mesh.materials[0].baseColorFactor.x, 1.0f) &&
                NearlyEqual(mesh.materials[0].baseColorFactor.y, 1.0f) &&
                NearlyEqual(mesh.materials[0].baseColorFactor.z, 1.0f),
            "A material without an explicit factor should preserve the glTF white default.");
    Require(NearlyEqual(mesh.materials[3].baseColorFactor.x, 0.2f) &&
                NearlyEqual(mesh.materials[3].baseColorFactor.y, 0.4f) &&
                NearlyEqual(mesh.materials[3].baseColorFactor.z, 0.6f),
            "A glTF material without an albedo texture should preserve its baseColorFactor.");
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

void TestImportDiagnostics()
{
    using Json = nlohmann::json;
    const std::filesystem::path source = FixturePath().parent_path() / "surface-transforms.gltf";
    std::ifstream input(source);
    const Json baseline = Json::parse(input);
    const std::filesystem::path temporary = std::filesystem::temp_directory_path() /
        ("RtPbrSurvey-gltf-diagnostics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gltf");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{temporary};
    const auto run = [&](const Json& document, const char* code, GltfDiagnosticSeverity severity)
    {
        {
            std::ofstream output(temporary, std::ios::binary);
            output << document.dump(2) << "\r\n";
            Require(static_cast<bool>(output), "Temporary diagnostic fixture must be written.");
        }
        const Engine::GltfSceneAssetLoadResult result = Engine::LoadGltfSceneAsset(temporary.string());
        Require(static_cast<bool>(result) == (severity == GltfDiagnosticSeverity::Warning),
                "Warnings must permit import, while unsafe input must fail.");
        bool found = false;
        for (const GltfImportDiagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.code == code && diagnostic.severity == severity)
            {
                Require(!diagnostic.location.empty() && !diagnostic.message.empty(),
                        "Each diagnostic must identify its target and explain the policy.");
                found = true;
            }
        }
        Require(found, code);
        if (severity == GltfDiagnosticSeverity::Error)
        {
            Require(result.status == Engine::GltfSceneAssetLoadStatus::UnsupportedData && !result.message.empty(),
                    "Rejected data must have an explicit load status and error message.");
            GltfMeshData mesh;
            mesh.vertices.resize(1);
            std::vector<GltfImportDiagnostic> diagnostics;
            Require(!::LoadGltfMesh(temporary.string(), mesh, &diagnostics) && mesh.vertices.empty(),
                    "The legacy importer must apply the same rejection policy and clear stale output.");
            Require(!diagnostics.empty(), "Legacy callers must be able to retrieve structured diagnostics.");
        }
    };
    const Engine::GltfSceneAssetLoadResult clean = Engine::LoadGltfSceneAsset(source.string());
    Require(clean && clean.diagnostics.empty(), "Supported opaque packed-float input must remain diagnostic-free.");
    Json document = baseline;
    document["materials"][0]["alphaMode"] = "MASK";
    run(document, "AlphaMaskNativePtOnly", GltfDiagnosticSeverity::Warning);
    document["materials"][0]["alphaMode"] = "BLEND";
    run(document, "AlphaModeIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["materials"][0]["doubleSided"] = true;
    run(document, "DoubleSidedNativePtOnly", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["materials"][0]["normalTexture"]["texCoord"] = 1;
    run(document, "TextureCoordinateIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["materials"][0]["normalTexture"]["extensions"]["KHR_texture_transform"] = { {"offset", {0.1, 0.2}} };
    run(document, "TextureExtensionIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["materials"][0]["extensions"]["EXT_test_unsupported"] = Json::object();
    document["extensionsUsed"] = {"EXT_test_unsupported"};
    run(document, "MaterialExtensionIgnored", GltfDiagnosticSeverity::Warning);
    document["extensionsRequired"] = {"EXT_test_unsupported"};
    run(document, "RequiredExtension", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_unlit"] = Json::object();
    document["extensionsUsed"] = {"KHR_materials_unlit"};
    document["extensionsRequired"] = {"KHR_materials_unlit"};
    {
        std::ofstream output(temporary, std::ios::binary);
        output << document.dump(2) << "\r\n";
        Require(static_cast<bool>(output), "Unlit fixture must be written.");
    }
    const auto unlit = Engine::LoadGltfSceneAsset(temporary.string());
    Require(unlit && unlit.diagnostics.empty(), "Opaque Unlit must be accepted as a required extension.");
    Engine::SceneBuilder flattened, node;
    Require(flattened.AddGltfMesh(temporary.string()).has_value() && flattened.GetMesh().materials[0].unlit,
            "Flattened import must preserve Unlit.");
    Require(static_cast<bool>(node.AddGltfNodeMesh(unlit.asset, "Scaled")) && node.GetMesh().materials[0].unlit,
            "Node import must preserve Unlit.");
    GltfMeshData litMesh;
    Require(::LoadGltfMesh(source.string(), litMesh) && !litMesh.materials[0].unlit,
            "Ordinary materials must remain lit.");
    document = baseline;
    document["meshes"][0]["primitives"][0]["attributes"].erase("TANGENT");
    run(document, "MissingTangents", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL");
    run(document, "MissingNormals", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
    run(document, "MissingTexcoords", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["textures"][0]["sampler"] = 0;
    document["samplers"] = {{{"wrapS", 33071}, {"wrapT", 33071}}};
    run(document, "SamplerIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["meshes"][0]["primitives"][0]["targets"] = {{{"POSITION", 0}}};
    run(document, "MorphTargetsIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["nodes"][0]["skin"] = 0;
    document["skins"] = {{{"joints", {0}}}};
    run(document, "SkinIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["animations"] = {{{"samplers", Json::array()}, {"channels", Json::array()}}};
    run(document, "AnimationIgnored", GltfDiagnosticSeverity::Warning);
    document = baseline;
    document["meshes"][0]["primitives"][0]["mode"] = 5;
    run(document, "UnsupportedPrimitiveMode", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["bufferViews"][0]["byteStride"] = 24;
    run(document, "UnsupportedStride", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["accessors"][1]["componentType"] = 5123;
    run(document, "UnsupportedAccessor", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["accessors"][1]["count"] = 2;
    run(document, "UnsupportedAccessor", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["accessors"][0]["count"] = 100;
    run(document, "InvalidAccessorBounds", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["meshes"][0]["primitives"][0]["indices"] = -1;
    run(document, "MissingIndices", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["accessors"][4]["count"] = 2;
    run(document, "InvalidTriangleCount", GltfDiagnosticSeverity::Error);
    document = baseline;
    std::string uri = document["buffers"][0]["uri"].get<std::string>();
    uri.replace(uri.size() - 8, 8, "AAABAGQA");
    document["buffers"][0]["uri"] = uri;
    run(document, "IndexOutOfRange", GltfDiagnosticSeverity::Error);
    document = baseline;
    uri = document["buffers"][0]["uri"].get<std::string>();
    uri.replace(uri.find(',') + 1, 8, "AADAfwAA");
    document["buffers"][0]["uri"] = uri;
    run(document, "NonfiniteAttribute", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["accessors"][0]["sparse"] = {{"count", 1}, {"indices", {{"bufferView", 4}, {"componentType", 5123}}},
                                           {"values", {{"bufferView", 0}}}};
    run(document, "UnsupportedAccessor", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["nodes"][0]["children"] = {0};
    run(document, "NodeCycle", GltfDiagnosticSeverity::Error);
    document = baseline;
    document["meshes"][0]["primitives"][0]["material"] = 10;
    run(document, "InvalidMaterial", GltfDiagnosticSeverity::Error);
}

void TestThinTransmissionImport()
{
    using Json = nlohmann::json;
    std::ifstream input(FixturePath().parent_path() / "surface-transforms.gltf");
    Json baseline = Json::parse(input);
    baseline["materials"][0] = {
        {"pbrMetallicRoughness", {{"baseColorFactor", {0.2, 0.4, 0.8, 1.0}},
                                 {"metallicFactor", 0.0}, {"roughnessFactor", 0.0}}},
        {"extensions", {{"KHR_materials_transmission", {{"transmissionFactor", 0.5}}}}}};
    baseline["extensionsUsed"] = {"KHR_materials_transmission"};
    baseline["extensionsRequired"] = {"KHR_materials_transmission"};
    const auto temporary = std::filesystem::temp_directory_path() /
        ("RtPbrSurvey-thin-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gltf");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{temporary};
    const auto load = [&](const Json& document, bool success, const char* expectedCode)
    {
        {
            std::ofstream output(temporary, std::ios::binary);
            output << document.dump(2) << "\r\n";
            Require(static_cast<bool>(output), "Thin import fixture must be written.");
        }
        auto result = Engine::LoadGltfSceneAsset(temporary.string());
        Require(static_cast<bool>(result) == success, "Thin import acceptance must match the declared subset.");
        bool found = expectedCode == nullptr;
        for (const auto& diagnostic : result.diagnostics)
        {
            found |= expectedCode != nullptr && diagnostic.code == expectedCode &&
                diagnostic.severity == (success ? GltfDiagnosticSeverity::Warning : GltfDiagnosticSeverity::Error);
        }
        Require(found, "Expected a specific structured thin import diagnostic.");
        if (!success)
        {
            Require(result.status == Engine::GltfSceneAssetLoadStatus::UnsupportedData && !result.asset.IsValid(),
                    "Rejected thin data must not publish a scene asset.");
            GltfMeshData stale;
            stale.vertices.resize(1);
            Require(!LoadGltfMesh(temporary.string(), stale) && stale.vertices.empty(),
                    "Legacy loading must also reject unsupported thin data and clear stale output.");
        }
        return result;
    };
    const auto result = load(baseline, true, "ThinTransmissionApproximation");
    const auto check = [](const Engine::SceneMaterial& material, float factor, float ior)
    {
        Require(material.thinTransmission && NearlyEqual(material.transmissionFactor, factor) &&
                    NearlyEqual(material.transmissionTint.x, 0.2f) && NearlyEqual(material.transmissionTint.y, 0.4f) &&
                    NearlyEqual(material.transmissionTint.z, 0.8f) && NearlyEqual(material.transmissionIor, ior),
                "Constant factor, linear base-color tint and IOR must survive material conversion.");
    };
    Engine::SceneBuilder flattened, node, legacy;
    Require(flattened.AddGltfMesh(temporary.string()).has_value(), "Flattened thin asset must load.");
    Require(static_cast<bool>(node.AddGltfNodeMesh(result.asset, "Scaled")), "Thin node asset must load.");
    Require(legacy.LoadGltfMesh(temporary.string()), "Legacy thin scene must load.");
    check(flattened.GetMesh().materials[0], 0.5f, 1.5f);
    check(node.GetMesh().materials[0], 0.5f, 1.5f);
    check(legacy.GetMesh().materials[0], 0.5f, 1.5f);
    GltfMeshData mesh;
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].thinTransmission,
            "Raw glTF material must retain the thin marker.");
    Require(mesh.materials[0].transmissionTexIndex == -1 && mesh.materials[0].transmissionTintTexIndex == -1,
            "Constant transmission must keep neutral texture defaults.");

    Json document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] = 1;
    document["materials"][0]["extensions"]["KHR_materials_ior"] = {{"ior", 1.33}};
    document["extensionsRequired"].push_back("KHR_materials_ior");
    const auto customIor = load(document, true, "ThinTransmissionApproximation");
    Engine::SceneBuilder iorNode;
    Require(static_cast<bool>(iorNode.AddGltfNodeMesh(customIor.asset, "Scaled")), "Custom IOR node must load.");
    check(iorNode.GetMesh().materials[0], 1.0f, 1.33f);
    document["materials"][0]["extensions"]["KHR_materials_ior"] = Json::object();
    load(document, true, "ThinTransmissionApproximation");
    document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_transmission"] = Json::object();
    document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = 1;
    load(document, true, nullptr);
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].transmissionFactor == 0.0f,
            "Omitted transmissionFactor must default to zero.");

    for (const Json& invalid : {Json(-0.1), Json(1.1), Json(true), Json("1"), Json(nullptr)})
    {
        document = baseline;
        document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    for (const Json& invalid : {Json(0), Json(0.5), Json(1e300), Json(true), Json("1.5")})
    {
        document = baseline;
        document["materials"][0]["extensions"]["KHR_materials_ior"] = {{"ior", invalid}};
        load(document, false, "TransmissionMaterial");
    }
    for (double roughness : {0.1, 0.5, 1.0})
    {
        document = baseline;
        document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = roughness;
        load(document, true, "ThinTransmissionApproximation");
        Require(LoadGltfMesh(temporary.string(), mesh) && NearlyEqual(mesh.materials[0].roughnessFactor, static_cast<float>(roughness)),
                "Rough transmission must preserve the imported roughness.");
    }
    for (double roughness : {-0.1, 1.1})
    {
        document = baseline;
        document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = roughness;
        load(document, false, "TransmissionMaterial");
    }
    for (double metallic : {0.1, 0.5, 1.0})
    {
        document = baseline;
        document["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = metallic;
        load(document, true, "ThinTransmissionApproximation");
        Require(LoadGltfMesh(temporary.string(), mesh) && NearlyEqual(mesh.materials[0].metallicFactor, static_cast<float>(metallic)),
                "Thin metallic mixtures must preserve their factor.");
    }
    for (double metallic : {-0.1, 1.1})
    {
        document = baseline;
        document["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = metallic;
        load(document, false, "TransmissionMaterial");
    }
    for (const char* extension : {"KHR_materials_unlit", "KHR_materials_clearcoat"})
    {
        document = baseline;
        document["materials"][0]["extensions"][extension] = Json::object();
        load(document, false, "TransmissionMaterial");
    }
    document = baseline;
    document["extensionsUsed"].push_back("KHR_materials_volume");
    document["extensionsRequired"].push_back("KHR_materials_volume");
    document["materials"][0]["extensions"]["KHR_materials_volume"] = Json::object();
    load(document, true, "ThinTransmissionApproximation");
    document["materials"][0]["extensions"]["KHR_materials_volume"] = {
        {"thicknessFactor", 0.0}, {"attenuationColor", {0.25, 0.5, 0.8}}, {"attenuationDistance", 2.0}};
    load(document, true, "ThinTransmissionApproximation");
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].thinTransmission &&
                NearlyEqual(mesh.materials[0].transmissionFactor, 0.5f),
            "Zero-thickness volume must preserve the existing thin transmission path.");
    const Json zeroVolume = document;
    for (const Json& invalid : {Json(-1), Json(true), Json("0"), Json(nullptr), Json(1e300), Json(1)})
    {
        document = zeroVolume;
        document["materials"][0]["extensions"]["KHR_materials_volume"]["thicknessFactor"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    for (const Json& invalid : {Json(0), Json(-1), Json(true), Json(nullptr), Json(1e300), Json(1e-300)})
    {
        document = zeroVolume;
        document["materials"][0]["extensions"]["KHR_materials_volume"]["attenuationDistance"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    for (const Json& invalid : {Json::array({0, 1}), Json::array({0, 1, 2}), Json::array({true, 0, 1}),
             Json::array({-0.1, 0, 1}), Json("white")})
    {
        document = zeroVolume;
        document["materials"][0]["extensions"]["KHR_materials_volume"]["attenuationColor"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    document = zeroVolume;
    document["materials"][0]["extensions"]["KHR_materials_volume"]["thicknessTexture"] = {{"index", 0}};
    load(document, false, "TransmissionMaterial");
    document = zeroVolume;
    document["materials"][0]["extensions"]["KHR_materials_volume"] = nullptr;
    load(document, false, "TransmissionMaterial");
    document = zeroVolume;
    document["materials"][0]["extensions"].erase("KHR_materials_transmission");
    load(document, true, nullptr);
    document["materials"][0]["extensions"]["KHR_materials_volume"]["thicknessFactor"] = 1.0;
    load(document, false, "TransmissionMaterial");
    document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionTexture"] = {{"index", 0}};
    document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", 0}};
    const auto textured = load(document, true, "ThinTransmissionApproximation");
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].transmissionTexIndex == 0 &&
                mesh.materials[0].transmissionTintTexIndex == 0,
            "Transmission and color may use the same image with distinct sampling semantics.");
    Engine::SceneBuilder remapped;
    Require(remapped.AddGltfMesh(FixturePath().string()).has_value(), "Texture-remap prefix must load.");
    const size_t textureBase = remapped.GetMesh().textures.size();
    const size_t materialBase = remapped.GetMesh().materials.size();
    Require(static_cast<bool>(remapped.AddGltfNodeMesh(textured.asset, "Scaled")), "Textured transmission node must load.");
    const auto& texturedMaterial = remapped.GetMesh().materials[materialBase];
    Require(texturedMaterial.transmissionTexIndex == static_cast<int>(textureBase) &&
                texturedMaterial.transmissionTintTexIndex == static_cast<int>(textureBase),
            "Both transmission texture indices must be globally remapped.");
    Engine::SceneBuilder texturedLegacy;
    Require(texturedLegacy.LoadGltfMesh(temporary.string()) &&
                texturedLegacy.GetMesh().materials[0].transmissionTexIndex == 0 &&
                texturedLegacy.GetMesh().materials[0].transmissionTintTexIndex == 0,
            "Legacy scene loading must retain both texture indices.");
    Json texturedDocument = document;
    document = baseline;
    document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = 0.8;
    document["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {{"index", 0}};
    const auto roughTextured = load(document, true, "ThinTransmissionApproximation");
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].metallicRoughnessTexIndex == 0 &&
                NearlyEqual(mesh.materials[0].roughnessFactor, 0.8f) && mesh.materials[0].metallicFactor == 0.0f,
            "Nonmetallic rough transmission must retain the core roughness texture and factor.");
    Engine::SceneBuilder roughRemapped;
    Require(roughRemapped.AddGltfMesh(FixturePath().string()).has_value(), "Roughness remap prefix must load.");
    const size_t roughTextureBase = roughRemapped.GetMesh().textures.size();
    const size_t roughMaterialBase = roughRemapped.GetMesh().materials.size();
    Require(static_cast<bool>(roughRemapped.AddGltfNodeMesh(roughTextured.asset, "Scaled")) &&
                roughRemapped.GetMesh().materials[roughMaterialBase].metallicRoughnessTexIndex == static_cast<int>(roughTextureBase),
            "Rough transmission texture must use the global remapped index.");
    Engine::SceneBuilder roughLegacy;
    Require(roughLegacy.LoadGltfMesh(temporary.string()) &&
                roughLegacy.GetMesh().materials[0].metallicRoughnessTexIndex == 0,
            "Legacy loading must retain the rough transmission texture.");
    const Json roughDocument = document;
    document["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
    load(document, false, "MissingTransmissionTexcoords");
    for (const Json& invalid : {Json{{"index", 999999}}, Json{{"index", 0}, {"texCoord", 1}},
                               Json{{"index", 0}, {"extensions", {{"KHR_texture_transform", Json::object()}}}}})
    {
        document = roughDocument;
        document["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    document = roughDocument;
    document["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = 0.5;
    load(document, true, "ThinTransmissionApproximation");
    Require(LoadGltfMesh(temporary.string(), mesh) && NearlyEqual(mesh.materials[0].metallicFactor, 0.5f) &&
                mesh.materials[0].metallicRoughnessTexIndex == 0,
            "Metallic-roughness textures and metallic factors must remain coupled for thin mixtures.");
    document["materials"][0]["pbrMetallicRoughness"].erase("metallicFactor");
    load(document, true, "ThinTransmissionApproximation");
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].metallicFactor == 1.0f,
            "The glTF default metallic factor remains one even with transmission.");
    document = roughDocument;
    document["textures"][0]["source"] = 999999;
    load(document, false, "TransmissionMaterial");
    document = texturedDocument;
    document["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
    load(document, false, "MissingTransmissionTexcoords");
    for (const Json& invalid : {Json(nullptr), Json(0), Json::object(), Json{{"index", nullptr}},
                               Json{{"index", true}}, Json{{"index", 0.5}}, Json{{"index", -1}},
                               Json{{"index", 999999}}, Json{{"index", 0}, {"texCoord", 1}},
                               Json{{"index", 0}, {"texCoord", nullptr}},
                               Json{{"index", 0}, {"extensions", {{"KHR_texture_transform", Json::object()}}}}})
    {
        document = baseline;
        document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionTexture"] = invalid;
        load(document, false, "TransmissionMaterial");
    }
    document = texturedDocument;
    document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 1;
    load(document, false, "TransmissionMaterial");
    document = texturedDocument;
    document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["extensions"] =
        {{"KHR_texture_transform", {{"scale", {2, 2}}}}};
    load(document, false, "TransmissionMaterial");
    document = texturedDocument;
    document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] = 1;
    document["materials"][0]["extensions"]["KHR_materials_ior"] = {{"ior", 1.33}};
    load(document, false, "TransmissionMaterial");
    document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_transmission"] = 7;
    load(document, false, "TransmissionMaterial");
    document = baseline;
    document["materials"][0]["extensions"]["KHR_materials_ior"] = {{"ior", 1.33}};
    load(document, false, "TransmissionMaterial");
    document = baseline;
    document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", 0}};
    load(document, true, "ThinTransmissionApproximation");
    document = baseline;
    document["materials"][0]["alphaMode"] = "BLEND";
    load(document, false, "TransmissionMaterial");
    document = baseline;
    document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = 0.5;
    load(document, true, "ThinTransmissionApproximation");
    document["materials"][0]["alphaMode"] = "MASK";
    document["materials"][0]["doubleSided"] = true;
    document["materials"][0]["alphaCutoff"] = 0.25;
    const auto masked = load(document, true, "AlphaMaskNativePtOnly");
    Require(LoadGltfMesh(temporary.string(), mesh) && mesh.materials[0].alphaMask && mesh.materials[0].doubleSided &&
                NearlyEqual(mesh.materials[0].alphaCutoff, 0.25f), "Alpha mode, cutoff and sidedness must survive raw loading.");
    Engine::SceneBuilder maskNode, maskLegacy;
    Require(static_cast<bool>(maskNode.AddGltfNodeMesh(masked.asset, "Scaled")) &&
                maskNode.GetMesh().materials[0].alphaMask && maskNode.GetMesh().materials[0].doubleSided &&
                NearlyEqual(maskNode.GetMesh().materials[0].alphaCutoff, 0.25f), "Node loading must preserve alpha coverage.");
    Require(maskLegacy.LoadGltfMesh(temporary.string()) && maskLegacy.GetMesh().materials[0].alphaMask &&
                NearlyEqual(maskLegacy.GetMesh().materials[0].alphaCutoff, 0.25f), "Legacy loading must preserve alpha cutoff.");
    const Json maskDocument = document;
    for (double invalid : {-0.1, 1e300})
    {
        document = maskDocument;
        document["materials"][0]["alphaCutoff"] = invalid;
        load(document, false, "AlphaMaskMaterial");
    }
    document = maskDocument;
    document["materials"][0]["alphaCutoff"] = 2.0;
    load(document, true, "AlphaMaskNativePtOnly");
    document = maskDocument;
    document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", 0}};
    document["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
    load(document, false, "MissingAlphaMaskTexcoords");
    document = baseline;
    document["extensionsRequired"].push_back("KHR_lights_punctual");
    load(document, false, "RequiredExtension");
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

        TestVolumeMeshTopology();
        TestConvexVolumeBounds();
        TestSolidVolumeImport();
        TestNodeEnumerationAndFailures();
        TestIndependentNodeMeshesAndLifetime();
        TestGltfRotationAndHandednessConversion();
        TestSurfaceTransforms();
        TestExistingFlattenedMeshContract();
        TestImportDiagnostics();
        TestThinTransmissionImport();
    }
    catch (const std::exception& error)
    {
        std::cerr << "glTF node mesh tests failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
