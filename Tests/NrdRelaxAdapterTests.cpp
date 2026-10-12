#include "Renderer/NrdRelaxAdapter.h"

#include <cstring>
#include <iostream>
#include <limits>
#include "Scene/Scene.h"

namespace
{
bool CheckSnapshotLifecycle()
{
    Engine::SceneMesh mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {2, 0, 0};
    mesh.vertices[2].position = {0, 2, 0};
    mesh.indices = {0, 1, 2};
    Engine::InstanceData instance = {};
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixIdentity());
    Engine::NrdRelaxGeometrySnapshot snapshot;
    bool passed = snapshot.Token() == 0;
    const auto& uploaded = snapshot.CaptureUpload(mesh);
    passed &= uploaded.vertices.size() == 3 && snapshot.Token() == 0;
    mesh.vertices[0].position.z = 1000;
    passed &= uploaded.vertices[0].position.z == 0;
    snapshot.RecordTlas({&instance, 1});
    const uint64_t first = snapshot.Token();
    auto context = snapshot.MakeRangeContext(first, {.25f, .25f, .001f}, {0, 0, 1});
    using Disposition = Engine::NrdRelaxDistanceDisposition;
    const auto inspect = [&]()
    { return Engine::InspectNrdRelaxDistance(0, 2, 100, 0, 100, true, true, &context).disposition; };
    passed &= first != 0 && inspect() == Disposition::EnvironmentMissCandidate;
    snapshot.RecordTlas({&instance, 1});
    passed &= snapshot.Token() != first && inspect() == Disposition::EnvironmentMissPolicyRequired;
    context = snapshot.MakeRangeContext(first, {.25f, .25f, .001f}, {0, 0, 1});
    passed &= !context.geometrySnapshotMatches;
    instance.world._34 = 200;
    snapshot.RecordTlas({&instance, 1});
    context = snapshot.MakeRangeContext(snapshot.Token(), {.25f, .25f, .001f}, {0, 0, 1});
    passed &= inspect() == Disposition::EnvironmentMissPolicyRequired;
    snapshot.Invalidate();
    passed &= snapshot.Token() == 0 && inspect() == Disposition::EnvironmentMissPolicyRequired;
    instance.world._34 = 0;
    snapshot.RecordTlas({&instance, 1});
    context = snapshot.MakeRangeContext(snapshot.Token(), {.25f, .25f, .001f}, {0, 0, 1});
    passed &= inspect() == Disposition::EnvironmentMissCandidate;
    snapshot.Reset();
    passed &= snapshot.Token() == 0 && inspect() == Disposition::EnvironmentMissPolicyRequired;
    snapshot.RecordTlas({&instance, 1});
    passed &= snapshot.Token() == 0;
    snapshot.CaptureUpload(mesh);
    snapshot.RecordTlas({&instance, 1});
    context = snapshot.MakeRangeContext(snapshot.Token(), {.25f, .25f, .001f}, {0, 0, 1});
    passed &= inspect() == Disposition::EnvironmentMissPolicyRequired;
    snapshot.RecordTlas({});
    passed &= snapshot.Token() == 0;
    return passed;
}

bool CheckSceneRange()
{
    Engine::SceneMesh mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {2, 0, 0};
    mesh.vertices[2].position = {0, 2, 0};
    mesh.indices = {0, 1, 2};
    mesh.ranges.push_back({0, 3, 0, 3});
    Engine::Scene scene;
    scene.mesh = &mesh;
    Engine::InstanceData instance = {};
    DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixIdentity());
    instance.prevWorld = instance.world;
    scene.instances.push_back(instance);
    auto bounds = Engine::BuildNrdRelaxWorldBounds(scene);
    bool passed = bounds.complete && bounds.minimum[0] < 0 && bounds.maximum[0] > 2;
    Engine::NrdRelaxRayRangeContext context;
    context.bounds = &bounds;
    context.origin = {.25f, .25f, .001f};
    context.direction = {0, 0, 1};
    const auto inspect = [&](float maximum)
    { return Engine::InspectNrdRelaxDistance(0, 2, maximum, 0, maximum, true, true, &context); };
    using Disposition = Engine::NrdRelaxDistanceDisposition;
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.geometrySnapshotMatches = true;
    passed &= inspect(100).disposition == Disposition::EnvironmentMissCandidate;
    passed &= inspect(100).hitDistance == 65504;
    passed &= inspect(1).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.direction = {0, 0, .001f};
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.direction = {0, 0, 2};
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.direction = {0, 0, 0};
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.direction = {0, 0, 1};
    context.origin = {1000, 0, 0};
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.origin = {.25f, .25f, .001f};
    context.direction[0] = std::numeric_limits<float>::quiet_NaN();
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    context.direction = {0, 0, 1};
    bounds.complete = false;
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    bounds = Engine::BuildNrdRelaxWorldBounds(scene);
    const auto moved = DirectX::XMMatrixScaling(2, .5f, 1) * DirectX::XMMatrixRotationZ(DirectX::XM_PIDIV2) *
                       DirectX::XMMatrixTranslation(3, -4, 5);
    DirectX::XMStoreFloat4x4(&scene.instances[0].world, DirectX::XMMatrixTranspose(moved));
    auto transformed = Engine::BuildNrdRelaxWorldBounds(scene);
    passed &= transformed.complete;
    for (const auto& vertex : mesh.vertices)
    {
        DirectX::XMFLOAT3 point;
        DirectX::XMStoreFloat3(&point,
                               DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&vertex.position), moved));
        const float coordinates[] = {point.x, point.y, point.z};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            passed &= coordinates[axis] >= transformed.minimum[axis] && coordinates[axis] <= transformed.maximum[axis];
        }
    }
    scene.instances[0] = instance;
    instance.world._14 = 100;
    scene.instances.push_back(instance);
    bounds = Engine::BuildNrdRelaxWorldBounds(scene);
    passed &= bounds.complete && bounds.maximum[0] > 102;
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    scene.instances.pop_back();
    mesh.vertices.resize(6);
    mesh.vertices[3].position = {0, 0, 200};
    mesh.vertices[4].position = {2, 0, 200};
    mesh.vertices[5].position = {0, 2, 200};
    mesh.indices = {3, 4, 5};
    bounds = Engine::BuildNrdRelaxWorldBounds(scene);
    passed &= bounds.complete && bounds.maximum[2] > 200;
    passed &= inspect(100).disposition == Disposition::EnvironmentMissPolicyRequired;
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    mesh.indices[0] = UINT32_MAX;
    passed &= !Engine::BuildNrdRelaxWorldBounds(scene).complete;
    mesh.indices[0] = 0;
    scene.instances[0].meshId = 1;
    passed &= !Engine::BuildNrdRelaxWorldBounds(scene).complete;
    scene.instances[0].meshId = 0;
    scene.instances[0].world._41 = 1;
    passed &= !Engine::BuildNrdRelaxWorldBounds(scene).complete;
    scene.instances[0].world._41 = 0;
    mesh.vertices[0].position.x = std::numeric_limits<float>::infinity();
    passed &= !Engine::BuildNrdRelaxWorldBounds(scene).complete;
    mesh.vertices[0].position.x = 0;
    mesh.ranges.clear();
    passed &= Engine::BuildNrdRelaxWorldBounds(scene).complete;
    scene.instances.clear();
    passed &= !Engine::BuildNrdRelaxWorldBounds(scene).complete;
    return passed;
}
} // namespace

int main()
{
    const Engine::NrdRelaxSupportInfo defaults;
    const Engine::NrdRelaxSupportInfo info = Engine::QueryNrdRelaxSupport();
    bool passed = !defaults.available && !info.available &&
                  defaults.status == Engine::NrdRelaxSupportStatus::NotIntegrated &&
                  info.status == Engine::NrdRelaxSupportStatus::NotIntegrated && info.targetSdkVersion != nullptr &&
                  std::strcmp(info.targetSdkVersion, "4.17.3") == 0 && info.reason != nullptr && info.reason[0] != '\0';
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(0, true).ready;
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(4, true).ready;
    passed &= !Engine::QueryNrdRelaxSamplingReadiness(1, false).ready;
    passed &= Engine::QueryNrdRelaxSamplingReadiness(1, true).ready;
    passed &= !Engine::QueryNrdRelaxSupport().available;
    passed &= CheckSceneRange();
    passed &= CheckSnapshotLifecycle();
    using Disposition = Engine::NrdRelaxDistanceDisposition;
    const auto inspect = [](float distance, float state, float raw, bool primary = true, bool active = true)
    { return Engine::InspectNrdRelaxDistance(distance, state, raw, 0.0f, 100.0f, primary, active); };
    passed &= inspect(2.0f, 1.0f, 1.999f).disposition == Disposition::HitCandidate;
    passed &= inspect(2.0f, 1.0f, 1.999f).hitDistance == 2.0f;
    passed &= inspect(0.0f, 1.0f, 1.0f).disposition == Disposition::InvalidData;
    passed &= inspect(2.0f, 1.0f, 101.0f).disposition == Disposition::InvalidData;
    passed &= inspect(65505.0f, 1.0f, 50.0f).disposition == Disposition::PackingRangeExceeded;
    passed &= inspect(0.0f, 2.0f, 100.0f).disposition == Disposition::EnvironmentMissPolicyRequired;
    passed &= inspect(0.0f, 2.0f, 100.0f).hitDistance == 0.0f;
    passed &= inspect(0.0f, 2.0f, 99.0f).disposition == Disposition::InvalidData;
    passed &= inspect(1.0f, 2.0f, 100.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 3.0f, 0.0f).disposition == Disposition::ReplacementRayRequired;
    passed &= inspect(0.0f, 0.0f, 0.0f).disposition == Disposition::TraceRequired;
    passed &= inspect(0.0f, 0.0f, 0.0f, false).disposition == Disposition::ExcludePrimaryMiss;
    passed &= inspect(2.0f, 1.0f, 2.0f, false).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 4.0f, 0.0f, true, false).disposition == Disposition::InactiveLobe;
    passed &= inspect(0.0f, 4.0f, 0.0f, true, false).zeroRadianceRequired;
    passed &= inspect(0.0f, 4.0f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 2.0f, 100.0f, true, false).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 1.5f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(0.0f, 8.0f, 0.0f).disposition == Disposition::InvalidData;
    passed &= inspect(std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f).disposition == Disposition::InvalidData;
    passed &= Engine::InspectNrdRelaxDistance(1, 1, 1, 1, 100, true, true).disposition == Disposition::InvalidData;
    passed &= Engine::InspectNrdRelaxDistance(1, 1, 1, 0, 0, true, true).disposition == Disposition::InvalidData;
    if (!passed)
    {
        std::cerr << "NRD RELAX must remain unavailable until dispatch and inputs are integrated.\n";
    }
    return passed ? 0 : 1;
}
