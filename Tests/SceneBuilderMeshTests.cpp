#include "Scene/ProceduralSceneBuilder.h"
#include "Scene/SceneBuilder.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{

bool TestCylinderTopology()
{
    constexpr uint32_t segmentCount = 16;
    const Engine::SceneMesh capped =
        Engine::Procedural::CreateCylinderMesh(0.5f, 1.0f, segmentCount, Engine::CylinderCapMode::Both);
    const Engine::SceneMesh open =
        Engine::Procedural::CreateCylinderMesh(0.5f, 1.0f, segmentCount, Engine::CylinderCapMode::None);

    const size_t expectedSideVertexCount = segmentCount * 4;
    const size_t expectedSideIndexCount = segmentCount * 6;
    const size_t expectedCapVertexCount = 2 * (segmentCount + 2);
    const size_t expectedCapIndexCount = 2 * segmentCount * 3;
    return open.vertices.size() == expectedSideVertexCount &&
        open.indices.size() == expectedSideIndexCount &&
        capped.vertices.size() == expectedSideVertexCount + expectedCapVertexCount &&
        capped.indices.size() == expectedSideIndexCount + expectedCapIndexCount;
}

bool TestCubeAndCylinderInstances()
{
    Engine::SceneBuilder builder;
    const uint32_t materialId = builder.AddSolidColorMaterial(160, 170, 180, 255);
    const Engine::SceneMeshId cubeId = builder.AddCube(1.0f);
    const Engine::SceneMeshId cylinderId =
        builder.AddCylinder(0.5f, 1.0f, 16, Engine::CylinderCapMode::Both);

    builder.AddInstance(cubeId, DirectX::XMMatrixIdentity(), materialId);
    builder.AddInstance(
        cylinderId,
        DirectX::XMMatrixScaling(0.4f, 0.8f, 1.2f) * DirectX::XMMatrixTranslation(2.0f, 0.0f, 0.0f),
        materialId);

    const Engine::SceneMesh& mesh = builder.GetMesh();
    const Engine::Scene& scene = builder.GetScene();
    return cubeId == 0 && cylinderId == 1 && mesh.ranges.size() == 2 &&
        mesh.ranges[cubeId].indexCount == 36 && mesh.ranges[cylinderId].indexCount == 16 * 12 &&
        mesh.ranges[cubeId].firstIndex + mesh.ranges[cubeId].indexCount ==
            mesh.ranges[cylinderId].firstIndex &&
        scene.instances.size() == 2 && scene.instances[0].meshId == cubeId &&
        scene.instances[1].meshId == cylinderId && scene.instances[1].materialId == materialId;
}

bool TestLegacyCompositeCompatibility()
{
    Engine::SceneBuilder builder;
    const uint32_t materialId = builder.AddSolidColorMaterial(255, 255, 255, 255);
    builder.AppendCube(1.0f, materialId);
    builder.AppendSphere(0.5f, 4, 8, materialId);
    builder.AddInstance(DirectX::XMMatrixIdentity(), materialId);

    return builder.GetMesh().ranges.size() == 1 && builder.GetScene().instances.size() == 1 &&
        builder.GetScene().instances[0].meshId == 0 &&
        builder.GetMesh().ranges[0].indexCount == builder.GetMesh().indices.size();
}

bool TestValidation()
{
    bool rejectedSegments = false;
    bool rejectedMeshId = false;
    try
    {
        Engine::Procedural::CreateCylinderMesh(0.5f, 1.0f, 2);
    }
    catch (const std::invalid_argument&)
    {
        rejectedSegments = true;
    }

    try
    {
        Engine::SceneBuilder builder;
        const uint32_t materialId = builder.AddSolidColorMaterial(255, 255, 255, 255);
        builder.AddCube(1.0f);
        builder.AddInstance(99, DirectX::XMMatrixIdentity(), materialId);
    }
    catch (const std::out_of_range&)
    {
        rejectedMeshId = true;
    }
    return rejectedSegments && rejectedMeshId;
}

bool TestInstanceLayout()
{
    return sizeof(Engine::InstanceData) == 144 && offsetof(Engine::InstanceData, materialId) == 128 &&
        offsetof(Engine::InstanceData, meshId) == 132;
}

} // namespace

int main()
{
    if (!TestCylinderTopology() || !TestCubeAndCylinderInstances() || !TestLegacyCompositeCompatibility() ||
        !TestValidation() || !TestInstanceLayout())
    {
        std::cerr << "SceneBuilder mesh tests failed.\n";
        return 1;
    }
    return 0;
}
