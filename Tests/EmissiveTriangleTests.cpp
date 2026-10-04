#include "Scene/EmissiveTriangleTable.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{

void Require(bool value)
{
    if (!value)
    {
        throw std::runtime_error("Emitter table assertion failed.");
    }
}

template <typename Action>
void Reject(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    Require(rejected);
}

Engine::SceneMesh Mesh()
{
    Engine::SceneMesh mesh;
    for (const DirectX::XMFLOAT3 position : {DirectX::XMFLOAT3{0, 0, 0}, {1, 0, 0}, {0, 1, 0}})
    {
        Engine::SceneVertex vertex = {};
        vertex.position = position;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2};
    mesh.ranges.push_back({0, 3, 0, 3});
    Engine::SceneMaterial material;
    material.emissiveTexIndex = 0;
    mesh.materials.push_back(material);
    Engine::SceneTexture texture;
    texture.width = texture.height = 1;
    texture.pixels = {255, 255, 255, 255};
    mesh.textures.push_back(texture);
    return mesh;
}

Engine::InstanceData Instance(DirectX::FXMMATRIX world)
{
    Engine::InstanceData instance = {};
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixTranspose(world));
    return instance;
}

void TestInstances()
{
    Engine::SceneMesh mesh = Mesh();
    Engine::Scene scene;
    scene.mesh = &mesh;
    scene.instances = {Instance(DirectX::XMMatrixIdentity()),
        Instance(DirectX::XMMatrixScaling(2, 3, 4) * DirectX::XMMatrixTranslation(5, 0, 0))};
    const Engine::EmissiveTriangleTable table = Engine::BuildEmissiveTriangleTable(scene);
    Require(table.triangles.size() == 2 && table.totalArea == 3.5);
    Require(table.triangles[0].instanceId == 0 && table.triangles[1].instanceId == 1);
    Require(table.triangles[1].positions[0].x == 5 && table.triangles[1].area == 3);
    Require(std::abs(table.triangles[0].selectionPdf - 1.0 / 7) < 1e-12);
    Require(table.triangles.back().cumulativeProbability == 1);
    std::vector<Engine::SceneMaterial> overrides = mesh.materials;
    overrides[0].emissiveScale = 0;
    Require(Engine::BuildEmissiveTriangleTable(scene, overrides).triangles.empty());
    overrides[0].emissiveScale = 2;
    Require(Engine::BuildEmissiveTriangleTable(scene, overrides).triangles.size() == 2);
    overrides.push_back(overrides[0]);
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene, overrides); });
    mesh.materials[0].emissiveScale = 0;
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
    mesh.materials[0].emissiveScale = 1;
    mesh.textures[0].pixels = {0, 0, 0, 255};
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
    mesh.textures[0].pixels = {255, 0, 0, 255};
    mesh.materials[0].emissiveFactor = {0, 1, 0};
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
    mesh.materials[0].emissiveFactor = {1, 0, 0};
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.size() == 2);
    mesh.materials[0].emissiveTexIndex = -1;
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
    mesh.materials[0].emissiveTexIndex = 0;
    mesh.textures[0].width = 2;
    mesh.textures[0].pixels = {0, 0, 0, 255, 255, 0, 0, 255};
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.size() == 2);
}

void TestRanges()
{
    Engine::SceneMesh mesh = Mesh();
    mesh.materials.push_back(mesh.materials[0]);
    mesh.materials[0].emissiveScale = 0;
    for (uint32_t index = 0; index < 3; ++index)
    {
        Engine::SceneVertex vertex = mesh.vertices[index];
        vertex.materialId = 1;
        vertex.position.z = 2;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(), {3, 4, 5});
    mesh.ranges.push_back({3, 3, 3, 3});
    Engine::Scene scene;
    scene.mesh = &mesh;
    scene.instances = {Instance(DirectX::XMMatrixIdentity()), Instance(DirectX::XMMatrixIdentity())};
    scene.instances[1].meshId = 1;
    const auto table = Engine::BuildEmissiveTriangleTable(scene);
    Require(table.triangles.size() == 1 && table.triangles[0].materialId == 1);
    Require(table.triangles[0].instanceId == 1 && table.triangles[0].primitiveIndex == 0);
    Require(table.triangles[0].positions[0].z == 2);
    mesh.ranges[1].indexCount = 0;
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.size() == 1);
    mesh.indices[0] = 3;
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
}

void TestInvalidAndDegenerate()
{
    Engine::SceneMesh mesh = Mesh();
    Engine::Scene scene;
    scene.mesh = &mesh;
    scene.instances = {Instance(DirectX::XMMatrixIdentity())};
    mesh.vertices[2].position = mesh.vertices[1].position;
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
    mesh.vertices[2].position.x = std::numeric_limits<float>::infinity();
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    mesh = Mesh();
    scene.instances[0] = Instance(DirectX::XMMatrixScaling(-1, 1, 1));
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    scene.instances[0] = Instance(DirectX::XMMatrixScaling(0, 1, 1));
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    scene.instances[0] = Instance(DirectX::XMMatrixIdentity());
    mesh.textures[0].pixels.pop_back();
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    mesh = Mesh();
    mesh.vertices[0].materialId = 999;
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    mesh = Mesh();
    scene.instances[0].world._41 = 0.1f;
    Reject([&] { Engine::BuildEmissiveTriangleTable(scene); });
    scene.instances.clear();
    scene.mesh = nullptr;
    Require(Engine::BuildEmissiveTriangleTable(scene).triangles.empty());
}

void TestGpuSerialization()
{
    Engine::SceneMesh mesh = Mesh();
    Engine::Scene scene;
    scene.mesh = &mesh;
    scene.instances = {Instance(DirectX::XMMatrixIdentity()), Instance(DirectX::XMMatrixScaling(2, 3, 4))};
    Engine::EmissiveTriangleTable table = Engine::BuildEmissiveTriangleTable(scene);
    const auto gpu = Engine::SerializeEmissiveTriangleTable(table);
    Require(gpu.size() == 2 && sizeof(gpu[0]) == 88);
    Require(gpu[0].area == .5f && gpu[1].area == 3);
    Require(gpu[0].selectionPdf == gpu[0].cumulativeProbability);
    Require(gpu[1].selectionPdf == 1.0f - gpu[0].cumulativeProbability);
    Require(gpu[1].instanceId == 1 && gpu[1].materialId == 0);
    Require(Engine::SerializeEmissiveTriangleTable({}).empty());
    table.triangles[0].cumulativeProbability = 1.0 - 1e-10;
    Reject([&] { Engine::SerializeEmissiveTriangleTable(table); });
    table = Engine::BuildEmissiveTriangleTable(scene);
    table.triangles[0].area = 1e100;
    Reject([&] { Engine::SerializeEmissiveTriangleTable(table); });
}

} // namespace

int main()
{
    try
    {
        TestInstances();
        TestRanges();
        TestInvalidAndDegenerate();
        TestGpuSerialization();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
