#include "stdafx.h"

#include "SceneFactory.h"
#include "ProceduralSceneBuilder.h"
#include "SceneBuilder.h"
#include "SampleScene.h"

#include <DirectXMath.h>
#include <DirectXMathMatrix.inl>
#include <cassert>
#include <cstdint>
#include <memory>

using DirectX::XMFLOAT3;
using DirectX::XMStoreFloat4x4;
using DirectX::XMMATRIX;
using DirectX::XMMatrixIdentity;
using DirectX::XMMatrixScaling;
using DirectX::XMMatrixTranslation;
using DirectX::XMMatrixTranspose;

namespace Engine
{

namespace
{

class CornellBoxScene : public SampleScene
{
public:
    static constexpr int kMaxInstanceCount = 1;

    const char* Name() const override
    {
        return "Cornell Box + Mirror Ball";
    }

    void Load() override
    {
        m_mesh = {};
        m_scene = {};

        // Textures
        const int whiteTex = Procedural::AddSolidColorTexture(m_mesh, 255, 255, 255, 255);
        const int redTex   = Procedural::AddSolidColorTexture(m_mesh, 255, 0, 0, 255);
        const int greenTex = Procedural::AddSolidColorTexture(m_mesh, 0, 255, 0, 255);
        const int blackTex = Procedural::AddSolidColorTexture(m_mesh, 0, 0, 0, 255);

        // Materials
        // 0: Red wall
        m_mesh.materials.push_back(MakeMat(redTex,   whiteTex, blackTex, whiteTex, 0.8f,  0.0f, 0.0f));
        // 1: Green wall
        m_mesh.materials.push_back(MakeMat(greenTex, whiteTex, blackTex, whiteTex, 0.8f,  0.0f, 0.0f));
        // 2: White diffuse (back wall, floor, ceiling)
        m_mesh.materials.push_back(MakeMat(whiteTex, whiteTex, blackTex, whiteTex, 0.8f,  0.0f, 0.0f));
        // 3: Mirror ball (metallic)
        m_mesh.materials.push_back(MakeMat(whiteTex, whiteTex, blackTex, whiteTex, 0.02f, 1.0f, 0.0f));
        // 4: Diffuse sphere
        m_mesh.materials.push_back(MakeMat(whiteTex, whiteTex, blackTex, whiteTex, 0.9f,  0.0f, 0.0f));
        // 5: Emissive panel
        m_mesh.materials.push_back(MakeMat(whiteTex, whiteTex, whiteTex, whiteTex, 0.8f,  0.0f, 4.0f));

        m_mesh.materialIndex = 0;

        // Geometry
        const float halfW = 1.5f;
        const float halfH = 1.5f;
        const float halfD = 1.5f;

        // Left wall (red)
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{-halfW, 0.0f, 0.0f},
            XMFLOAT3{0.0f, 3.0f, 3.0f},
            XMFLOAT3{1.0f, 0.0f, 0.0f}, 0, true);
        // Right wall (green)
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{halfW, 0.0f, 0.0f},
            XMFLOAT3{0.0f, 3.0f, 3.0f},
            XMFLOAT3{-1.0f, 0.0f, 0.0f}, 1);
        // Back wall (white)
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{0.0f, 0.0f, -halfD},
            XMFLOAT3{3.0f, 3.0f, 0.0f},
            XMFLOAT3{0.0f, 0.0f, 1.0f}, 2, true);
        // Floor (white)
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{0.0f, -halfH, 0.0f},
            XMFLOAT3{3.0f, 0.0f, 3.0f},
            XMFLOAT3{0.0f, 1.0f, 0.0f}, 2);
        // Ceiling (white)
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{0.0f, halfH, 0.0f},
            XMFLOAT3{3.0f, 0.0f, 3.0f},
            XMFLOAT3{0.0f, -1.0f, 0.0f}, 2, true);
        // Mirror ball
        Procedural::AddSphere(m_mesh,
            XMFLOAT3{-0.4f, -0.2f, 0.5f}, 0.5f, 3);
        // Diffuse sphere
        Procedural::AddSphere(m_mesh,
            XMFLOAT3{0.6f, -0.9f, 0.0f}, 0.3f, 4);
        // Emissive panel on ceiling
        Procedural::AddQuad(m_mesh,
            XMFLOAT3{0.0f, halfH - 0.05f, 0.0f},
            XMFLOAT3{1.0f, 0.0f, 1.0f},
            XMFLOAT3{0.0f, -1.0f, 0.0f}, 5, true);

        m_scene.mesh = &m_mesh;
        Reset();
    }

    void Reset() override
    {
        m_scene.camera.pos = {0.0f, 0.0f, 5.0f};
        m_scene.camera.rot = {0.0f, 0.0f, 0.0f};
        m_scene.camera.fov = 50.0f;

        m_scene.instances.resize(1);
        XMStoreFloat4x4(&m_scene.instances[0].world, XMMatrixTranspose(XMMatrixIdentity()));
        m_scene.instances[0].prevWorld = m_scene.instances[0].world;
        m_scene.instances[0].materialId = 0;
    }

    void Update(float /*deltaTime*/, const SampleSceneUpdateContext& /*context*/) override
    {
    }

    Scene& GetScene() override { return m_scene; }
    const Scene& GetScene() const override { return m_scene; }
    SceneMesh& GetMesh() override { return m_mesh; }
    const SceneMesh& GetMesh() const override { return m_mesh; }

    int DisplayInstanceCount() const override { return 1; }
    int MaxDisplayInstanceCount() const override { return kMaxInstanceCount; }

    void SetDisplayInstanceCount(int count) override
    {
        assert(count <= kMaxInstanceCount);
        (void)count;
    }

    float DefaultMeshScale() const override { return 1.0f; }

private:
    static SceneMaterial MakeMat(int albedoTex, int mrTex, int emissiveTex,
                                 int occlusionTex,
                                 float rough, float metal, float emissive)
    {
        SceneMaterial mat = {};
        mat.albedoTexIndex = albedoTex;
        mat.metallicRoughnessTexIndex = mrTex;
        mat.emissiveTexIndex = emissiveTex;
        mat.occlusionTexIndex = occlusionTex;
        mat.normalTexIndex = -1;
        mat.roughnessFactor = rough;
        mat.metallicFactor = metal;
        mat.occlusionStrength = 1.0f;
        mat.ambientOcclusionFactor = 1.0f;
        mat.emissiveScale = emissive;
        return mat;
    }

    Scene m_scene;
    SceneMesh m_mesh;
};

class HostPrimitiveMeshScene : public SampleScene
{
public:
    const char* Name() const override
    {
        return "Host Primitive Meshes";
    }

    void Load() override
    {
        m_builder.Clear();
        const uint32_t floorMaterial = m_builder.AddSolidColorMaterial(90, 100, 110, 255);
        const uint32_t cubeMaterial = m_builder.AddSolidColorMaterial(70, 150, 220, 255);
        const uint32_t cylinderMaterial = m_builder.AddSolidColorMaterial(220, 120, 55, 255);
        const SceneMeshId cubeId = m_builder.AddCube(1.0f);
        const SceneMeshId cylinderId =
            m_builder.AddCylinder(0.5f, 1.0f, 16, CylinderCapMode::Both);

        m_builder.AddInstance(
            cubeId,
            DirectX::XMMatrixScaling(8.0f, 0.1f, 8.0f) * DirectX::XMMatrixTranslation(0.0f, -0.8f, 0.0f),
            floorMaterial);
        m_builder.AddInstance(
            cubeId,
            DirectX::XMMatrixTranslation(-1.25f, -0.2f, 0.0f),
            cubeMaterial);
        m_builder.AddInstance(
            cylinderId,
            DirectX::XMMatrixScaling(0.7f, 1.2f, 0.4f) * DirectX::XMMatrixTranslation(1.25f, -0.2f, 0.0f),
            cylinderMaterial);

        Reset();
    }

    void Reset() override
    {
        Scene& scene = m_builder.GetScene();
        scene.camera.pos = {0.0f, 2.0f, 6.0f};
        scene.camera.gazePoint = {0.0f, -0.2f, 0.0f};
        scene.camera.fov = 50.0f;
    }

    void Update(float, const SampleSceneUpdateContext&) override
    {
    }

    Scene& GetScene() override { return m_builder.GetScene(); }
    const Scene& GetScene() const override { return m_builder.GetScene(); }
    SceneMesh& GetMesh() override { return m_builder.GetMesh(); }
    const SceneMesh& GetMesh() const override { return m_builder.GetMesh(); }
    int DisplayInstanceCount() const override { return static_cast<int>(m_builder.GetScene().instances.size()); }
    int MaxDisplayInstanceCount() const override { return DisplayInstanceCount(); }
    void SetDisplayInstanceCount(int) override {}
    float DefaultMeshScale() const override { return 1.0f; }

private:
    SceneBuilder m_builder;
};

class HybridReflectionEstimatorTestScene : public SampleScene
{
public:
    const char* Name() const override
    {
        return "Hybrid Reflection Estimator Test";
    }

    void Load() override
    {
        m_builder.Clear();

        SceneMesh& mesh = m_builder.GetMesh();
        const int neutralAlbedo = Procedural::AddSolidColorTexture(mesh, 180, 180, 180, 255);
        const int darkAlbedo = Procedural::AddSolidColorTexture(mesh, 45, 50, 55, 255);
        const int white = Procedural::AddSolidColorTexture(mesh, 255, 255, 255, 255);
        const int black = Procedural::AddSolidColorTexture(mesh, 0, 0, 0, 255);

        const SceneMeshId sphere = m_builder.AddSphere(0.52f, 24, 48);
        const SceneMeshId cube = m_builder.AddCube(1.0f);

        constexpr float roughnessValues[] = {0.0f, 0.05f, 0.15f, 0.35f, 0.6f, 1.0f};
        constexpr float spacing = 1.35f;
        constexpr float firstX = -0.5f * spacing * static_cast<float>(_countof(roughnessValues) - 1);

        for (UINT row = 0; row < 2; ++row)
        {
            const float metallic = row == 0 ? 1.0f : 0.0f;
            const float y = row == 0 ? 0.75f : -0.55f;
            for (UINT column = 0; column < _countof(roughnessValues); ++column)
            {
                SceneMaterial material = MakeMaterial(
                    neutralAlbedo, white, black, white, roughnessValues[column], metallic, 0.0f);
                const uint32_t materialId = m_builder.AddMaterial(material);
                const float x = firstX + static_cast<float>(column) * spacing;
                m_builder.AddInstance(sphere, XMMatrixTranslation(x, y, 0.0f), materialId);
            }
        }

        const uint32_t floorMaterial =
            m_builder.AddMaterial(MakeMaterial(darkAlbedo, white, black, white, 0.9f, 0.0f, 0.0f));
        m_builder.AddInstance(cube,
                              XMMatrixScaling(10.0f, 0.1f, 5.0f) * XMMatrixTranslation(0.0f, -1.35f, 0.0f),
                              floorMaterial);

        const uint32_t emissiveMaterial =
            m_builder.AddMaterial(MakeMaterial(white, white, white, white, 1.0f, 0.0f, 12.0f));
        m_builder.AddInstance(cube,
                              XMMatrixScaling(0.18f, 2.5f, 0.18f) * XMMatrixTranslation(4.35f, 0.25f, 1.4f),
                              emissiveMaterial);

        Reset();
    }

    void Reset() override
    {
        Scene& scene = m_builder.GetScene();
        scene.camera.pos = {0.0f, 0.4f, 9.0f};
        scene.camera.gazePoint = {0.0f, -0.2f, 0.0f};
        scene.camera.fov = 46.0f;
        scene.camera.nearZ = 0.1f;
        scene.camera.farZ = 100.0f;
    }

    void Update(float, const SampleSceneUpdateContext&) override
    {
    }

    Scene& GetScene() override { return m_builder.GetScene(); }
    const Scene& GetScene() const override { return m_builder.GetScene(); }
    SceneMesh& GetMesh() override { return m_builder.GetMesh(); }
    const SceneMesh& GetMesh() const override { return m_builder.GetMesh(); }
    int DisplayInstanceCount() const override { return static_cast<int>(m_builder.GetScene().instances.size()); }
    int MaxDisplayInstanceCount() const override { return DisplayInstanceCount(); }
    void SetDisplayInstanceCount(int) override {}
    float DefaultMeshScale() const override { return 1.0f; }

private:
    static SceneMaterial MakeMaterial(int albedoTexture,
                                      int metallicRoughnessTexture,
                                      int emissiveTexture,
                                      int occlusionTexture,
                                      float roughness,
                                      float metallic,
                                      float emissiveScale)
    {
        SceneMaterial material = {};
        material.albedoTexIndex = albedoTexture;
        material.metallicRoughnessTexIndex = metallicRoughnessTexture;
        material.emissiveTexIndex = emissiveTexture;
        material.occlusionTexIndex = occlusionTexture;
        material.normalTexIndex = -1;
        material.roughnessFactor = roughness;
        material.metallicFactor = metallic;
        material.occlusionStrength = 1.0f;
        material.ambientOcclusionFactor = 1.0f;
        material.emissiveScale = emissiveScale;
        return material;
    }

    SceneBuilder m_builder;
};

} // namespace

std::unique_ptr<SampleScene> SceneFactory::CreateCornellBox()
{
    return std::make_unique<CornellBoxScene>();
}

std::unique_ptr<SampleScene> SceneFactory::CreateHostPrimitiveMeshes()
{
    return std::make_unique<HostPrimitiveMeshScene>();
}

std::unique_ptr<SampleScene> SceneFactory::CreateHybridReflectionEstimatorTest()
{
    return std::make_unique<HybridReflectionEstimatorTestScene>();
}

} // namespace Engine
