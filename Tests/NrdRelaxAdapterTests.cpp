#include "Renderer/NrdRelaxAdapter.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <filesystem>
#include "Scene/Scene.h"
#include <cmath>
#include <set>

namespace
{
bool CheckPixelGate()
{
    using Disposition = Engine::NrdRelaxPixelDisposition;
    Engine::NrdRelaxWorldBounds bounds;
    bounds.complete = true;
    bounds.minimum = {-1, -1, -1};
    bounds.maximum = {1, 1, 1};
    uint64_t token = 7;
    Engine::NrdRelaxRayRangeContext context;
    context.bounds = &bounds;
    context.origin = {0, 0, 0};
    context.direction = {0, 0, 1};
    context.geometrySnapshotMatches = true;
    context.currentSnapshotToken = &token;
    context.expectedSnapshotToken = token;
    std::array<Engine::NrdRelaxLobeInput, 2> lobes;
    for (auto& lobe : lobes)
    {
        lobe.physical = {1, 2, 3};
        lobe.materialFactor = {.5f, .5f, .5f};
        lobe.distance = {2, 1, 2, 0};
        lobe.rangeContext = &context;
    }
    const auto inspect = [&]() { return Engine::InspectNrdRelaxPixel(lobes, 100, true); };
    auto decision = inspect();
    bool passed =
        decision.disposition == Disposition::Convertible && decision.packed[0] == std::array<float, 4>{2, 4, 6, 2};
    token = 8;
    decision = inspect();
    passed &= decision.disposition == Disposition::SnapshotRequired && decision.packed == decltype(decision.packed){};
    token = 7;
    uint64_t otherToken = 7;
    auto otherContext = context;
    otherContext.currentSnapshotToken = &otherToken;
    lobes[1].rangeContext = &otherContext;
    passed &= inspect().disposition == Disposition::SnapshotRequired;
    lobes[1].rangeContext = &context;
    lobes[1].distance = {0, 3, 0, 0};
    decision = inspect();
    passed &= decision.disposition == Disposition::ReconstructionRequired && decision.packed[1][3] == 0 &&
              decision.packed[1][0] == 2;
    lobes[1].distance = {0, 0, 0, 0};
    passed &= inspect().disposition == Disposition::TraceRequired;
    passed &= inspect().packed == decltype(inspect().packed){};
    lobes[1].distance = {0, 2, 100, 0};
    passed &= inspect().disposition == Disposition::Convertible && inspect().packed[1][3] == 65504;
    bounds.maximum[2] = 200;
    auto environmentDecision = inspect();
    passed &= environmentDecision.disposition == Disposition::EnvironmentPolicyRequired;
    passed &= environmentDecision.packed == decltype(environmentDecision.packed){};
    bounds.maximum[2] = 1;
    lobes[1].distance = {0, 4, 0, 0};
    lobes[1].active = false;
    passed &= inspect().disposition == Disposition::InvalidData;
    lobes[1].physical = {};
    passed &= inspect().disposition == Disposition::Convertible && inspect().packed[1] == std::array<float, 4>{};
    lobes[1].distance = {};
    passed &= inspect().disposition == Disposition::InvalidData;
    lobes[1].distance = {0, 4, 0, 0};
    lobes[0].physical = {60000, 0, 0};
    lobes[0].materialFactor = {.02f, .5f, .5f};
    decision = inspect();
    passed &=
        decision.disposition == Disposition::PackingRangeExceeded && decision.packed == decltype(decision.packed){};
    lobes[1].physical[0] = std::numeric_limits<float>::quiet_NaN();
    auto invalidDecision = inspect();
    passed &= invalidDecision.disposition == Disposition::InvalidData;
    passed &= invalidDecision.packed == decltype(invalidDecision.packed){};
    for (auto& lobe : lobes)
    {
        lobe.physical = {};
        lobe.distance = {};
        lobe.rangeContext = nullptr;
    }
    auto missDecision = Engine::InspectNrdRelaxPixel(lobes, 100, false);
    passed &= missDecision.disposition == Disposition::ExcludePrimaryMiss;
    passed &= missDecision.packed == decltype(missDecision.packed){};
    lobes[1].physical[0] = 1;
    passed &= Engine::InspectNrdRelaxPixel(lobes, 100, false).disposition == Disposition::InvalidData;
    return passed;
}

bool CheckRadianceGate()
{
    using Disposition = Engine::NrdRelaxRadianceDisposition;
    const auto inspect = [](std::array<float, 3> physical, std::array<float, 3> factor, bool active = true)
    { return Engine::InspectNrdRelaxRadiance(physical, factor, active); };
    auto valid = inspect({1, 2, 3}, {.5f, .25f, .1f});
    bool passed = valid.disposition == Disposition::Packable && valid.demodulated[0] == 2 &&
                  valid.demodulated[1] == 8 && std::abs(valid.demodulated[2] - 30) < .0001f;
    passed &= inspect({0, 0, 0}, {.02f, .02f, .02f}, false).disposition == Disposition::InactiveLobe;
    passed &= inspect({1, 0, 0}, {.02f, .02f, .02f}, false).disposition == Disposition::InvalidData;
    passed &= inspect({65504, 0, 0}, {1, 1, 1}).disposition == Disposition::Packable;
    auto overflow = inspect({60000, 1, 2}, {.02f, .5f, .5f});
    passed &=
        overflow.disposition == Disposition::PackingRangeExceeded && overflow.demodulated == std::array<float, 3>{};
    passed &= inspect({0, 0, 0}, {0, 1, 1}).disposition == Disposition::InvalidData;
    passed &= inspect({0, 0, 0}, {1.1f, 1, 1}).disposition == Disposition::InvalidData;
    passed &= inspect({-1, 0, 0}, {1, 1, 1}).disposition == Disposition::InvalidData;
    passed &=
        inspect({std::numeric_limits<float>::infinity(), 0, 0}, {1, 1, 1}).disposition == Disposition::InvalidData;
    passed &=
        inspect({0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 1, 1}).disposition == Disposition::InvalidData;
    return passed;
}

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
    const auto gpuRange = snapshot.ExportGpuSceneRange(first);
    passed &= gpuRange.valid && gpuRange.minimum[0] <= 0 && gpuRange.maximum[0] >= 2 &&
              gpuRange.minimum[1] <= 0 && gpuRange.maximum[1] >= 2;
    Engine::Scene uploadedScene;
    uploadedScene.mesh = &uploaded;
    uploadedScene.instances = {instance};
    const auto cpuBounds = Engine::BuildNrdRelaxWorldBounds(uploadedScene);
    for (size_t axis = 0; axis < 3; ++axis)
    {
        passed &= gpuRange.minimum[axis] <= cpuBounds.minimum[axis] &&
                  gpuRange.maximum[axis] >= cpuBounds.maximum[axis];
    }
    passed &= !snapshot.ExportGpuSceneRange(0).valid && !snapshot.ExportGpuSceneRange(first + 1).valid;
    snapshot.RecordTlas({&instance, 1});
    passed &= snapshot.Token() != first && inspect() == Disposition::EnvironmentMissPolicyRequired;
    passed &= !snapshot.ExportGpuSceneRange(first).valid;
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

bool IsFiniteNumericArray(const nlohmann::json& value, size_t count)
{
    if (!value.is_array() || value.size() != count)
    {
        return false;
    }
    for (const auto& element : value)
    {
        if (!element.is_number() || !std::isfinite(element.get<float>()))
        {
            return false;
        }
    }
    return true;
}

bool ValidatePixelCorpus(const nlohmann::json& data)
{
    const std::set<std::string> expectedIds = {"cpu-pixel-convertible-base",
                                               "cpu-pixel-stale-snapshot",
                                               "cpu-pixel-trace-required",
                                               "cpu-pixel-reconstruction-required",
                                               "cpu-pixel-exclude-primary-miss",
                                               "cpu-snapshot-token-provenance",
                                               "cpu-scene-range-proof",
                                               "cpu-trace-provenance"};
    const std::set<std::string> hostIds = {
        "cpu-snapshot-token-provenance", "cpu-scene-range-proof", "cpu-trace-provenance"};
    if (!data.is_object() || !data.contains("schemaVersion") || data["schemaVersion"] != 1 || !data.contains("kind") ||
        data["kind"] != "nrd-pixel-contract-vectors" || !data.contains("vectors") || !data["vectors"].is_array())
    {
        std::cerr << "Invalid pixel corpus schema.\n";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& vector : data["vectors"])
    {
        if (!vector.is_object() || !vector.contains("id") || !vector["id"].is_string())
        {
            std::cerr << "Missing pixel contract case ID.\n";
            return false;
        }
        const std::string id = vector["id"].get<std::string>();
        const auto invalid = [&]()
        {
            std::cerr << "Invalid pixel contract case: " << id << "\n";
            return false;
        };
        if (!expectedIds.contains(id) || !seen.insert(id).second || !vector.contains("hostOnly") ||
            !vector["hostOnly"].is_boolean() || vector["hostOnly"].get<bool>() != hostIds.contains(id) ||
            !vector.contains("gpuRepresentable") || !vector["gpuRepresentable"].is_boolean())
        {
            return invalid();
        }
        if (hostIds.contains(id))
        {
            if (vector["gpuRepresentable"].get<bool>())
            {
                return invalid();
            }
            continue;
        }
        if (!vector.contains("expectedDisposition") || !vector["expectedDisposition"].is_string() ||
            !vector.contains("rayTMax") || !vector["rayTMax"].is_number() ||
            !std::isfinite(vector["rayTMax"].get<float>()) || !vector.contains("primaryHit") ||
            !vector["primaryHit"].is_boolean() || !vector.contains("lobes") || !vector["lobes"].is_array() ||
            vector["lobes"].size() != 2)
        {
            return invalid();
        }
        for (const auto& lobe : vector["lobes"])
        {
            if (!lobe.is_object() || !lobe.contains("physical") || !IsFiniteNumericArray(lobe["physical"], 3) ||
                !lobe.contains("materialFactor") || !IsFiniteNumericArray(lobe["materialFactor"], 3) ||
                !lobe.contains("distance") || !IsFiniteNumericArray(lobe["distance"], 4) ||
                (lobe.contains("active") && !lobe["active"].is_boolean()) || !lobe.contains("rangeContext"))
            {
                return invalid();
            }
            const auto& context = lobe["rangeContext"];
            if (!context.is_null() &&
                (!context.is_object() || !context.contains("snapshotToken") ||
                 !context["snapshotToken"].is_number_unsigned() || !context.contains("expectedSnapshotToken") ||
                 !context["expectedSnapshotToken"].is_number_unsigned() ||
                 !context.contains("geometrySnapshotMatches") || !context["geometrySnapshotMatches"].is_boolean() ||
                 (context.contains("boundsComplete") && !context["boundsComplete"].is_boolean())))
            {
                return invalid();
            }
        }
        const auto& disposition = vector["expectedDisposition"];
        if ((disposition == "Convertible" || disposition == "ReconstructionRequired") &&
            !vector.contains("expectedPacked"))
        {
            return invalid();
        }
        if (vector.contains("expectedPacked") &&
            (!vector["expectedPacked"].is_array() || vector["expectedPacked"].size() != 2 ||
             !IsFiniteNumericArray(vector["expectedPacked"][0], 4) ||
             !IsFiniteNumericArray(vector["expectedPacked"][1], 4)))
        {
            return invalid();
        }
    }
    if (seen != expectedIds)
    {
        std::cerr << "Pixel corpus is missing required case IDs.\n";
        return false;
    }
    return true;
}

bool CheckPixelContractVectors(const std::filesystem::path& corpusPath)
{
    std::ifstream input(corpusPath);
    if (!input)
    {
        std::cerr << "Unable to open NRD pixel contract vector corpus: " << corpusPath << "\n";
        return false;
    }
    nlohmann::json data;
    try
    {
        input >> data;
        if (!ValidatePixelCorpus(data))
        {
            return false;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Invalid NRD pixel contract vector corpus: " << corpusPath << ": " << error.what() << "\n";
        return false;
    }

    static const std::map<std::string, Engine::NrdRelaxPixelDisposition> dispositions = {
        {"InvalidData", Engine::NrdRelaxPixelDisposition::InvalidData},
        {"PackingRangeExceeded", Engine::NrdRelaxPixelDisposition::PackingRangeExceeded},
        {"SnapshotRequired", Engine::NrdRelaxPixelDisposition::SnapshotRequired},
        {"TraceRequired", Engine::NrdRelaxPixelDisposition::TraceRequired},
        {"EnvironmentPolicyRequired", Engine::NrdRelaxPixelDisposition::EnvironmentPolicyRequired},
        {"ExcludePrimaryMiss", Engine::NrdRelaxPixelDisposition::ExcludePrimaryMiss},
        {"ReconstructionRequired", Engine::NrdRelaxPixelDisposition::ReconstructionRequired},
        {"Convertible", Engine::NrdRelaxPixelDisposition::Convertible},
    };

    using Disposition = Engine::NrdRelaxPixelDisposition;
    size_t executed = 0;
    size_t hostOnly = 0;
    bool passed = true;
    for (const auto& vector : data["vectors"])
    {
        const std::string id = vector.value("id", "");
        if (id.empty())
        {
            std::cerr << "NRD pixel contract vector is missing an ID.\n";
            passed = false;
            continue;
        }
        if (vector.value("hostOnly", false))
        {
            ++hostOnly;
            std::cout << "Pixel contract host-only case (not GPU-verified): " << id << "\n";
            continue;
        }
        if (!vector.contains("expectedDisposition") || !vector["expectedDisposition"].is_string())
        {
            continue;
        }
        const std::string expectedDispositionName = vector["expectedDisposition"].get<std::string>();
        const auto dispositionIt = dispositions.find(expectedDispositionName);
        if (dispositionIt == dispositions.end())
        {
            std::cerr << "Pixel contract vector has an unknown expected disposition: " << id << "\n";
            passed = false;
            continue;
        }

        ++executed;
        float rayTMax = vector.value("rayTMax", 100.0f);
        bool primaryHit = vector.value("primaryHit", true);
        std::array<Engine::NrdRelaxLobeInput, 2> lobes = {};
        std::vector<std::shared_ptr<uint64_t>> tokens;
        std::vector<std::shared_ptr<Engine::NrdRelaxWorldBounds>> boundsStorage;
        std::vector<std::shared_ptr<Engine::NrdRelaxRayRangeContext>> contexts;
        std::map<uint64_t, std::shared_ptr<uint64_t>> tokenCache;
        tokens.reserve(2);
        boundsStorage.reserve(2);
        contexts.reserve(2);
        for (size_t index = 0; index < lobes.size(); ++index)
        {
            const auto& lobeJson = vector["lobes"][index];
            for (size_t channel = 0; channel < 3; ++channel)
            {
                lobes[index].physical[channel] = lobeJson["physical"][channel].get<float>();
                lobes[index].materialFactor[channel] = lobeJson["materialFactor"][channel].get<float>();
            }
            for (size_t channel = 0; channel < 4; ++channel)
            {
                lobes[index].distance[channel] = lobeJson["distance"][channel].get<float>();
            }
            lobes[index].active = lobeJson.value("active", true);
            const nlohmann::json* rangeJson = lobeJson.contains("rangeContext") && !lobeJson["rangeContext"].is_null()
                                                  ? &lobeJson["rangeContext"]
                                                  : nullptr;
            if (rangeJson != nullptr)
            {
                auto bounds = std::make_shared<Engine::NrdRelaxWorldBounds>();
                bounds->complete = rangeJson->value("boundsComplete", true);
                bounds->minimum = {-1.0, -1.0, -1.0};
                bounds->maximum = {1.0, 1.0, 1.0};
                uint64_t tokenValue = rangeJson->value("snapshotToken", 7ULL);
                auto cachedToken = tokenCache.find(tokenValue);
                std::shared_ptr<uint64_t> token;
                if (cachedToken == tokenCache.end())
                {
                    token = std::make_shared<uint64_t>(tokenValue);
                    tokenCache[tokenValue] = token;
                }
                else
                {
                    token = cachedToken->second;
                }
                auto context = std::make_shared<Engine::NrdRelaxRayRangeContext>();
                context->bounds = bounds.get();
                context->origin = {0, 0, 0};
                context->direction = {0, 0, 1};
                context->geometrySnapshotMatches = rangeJson->value("geometrySnapshotMatches", true);
                context->currentSnapshotToken = token.get();
                context->expectedSnapshotToken = rangeJson->value("expectedSnapshotToken", 7ULL);
                boundsStorage.push_back(bounds);
                tokens.push_back(token);
                contexts.push_back(context);
                lobes[index].rangeContext = context.get();
            }
            else
            {
                lobes[index].rangeContext = nullptr;
            }
        }

        auto decision = Engine::InspectNrdRelaxPixel(lobes, rayTMax, primaryHit);
        if (decision.disposition != dispositionIt->second)
        {
            std::cerr << "Pixel contract vector disposition mismatch: " << id << "\n";
            passed = false;
        }

        if (vector.contains("expectedPacked") && vector["expectedPacked"].is_array())
        {
            for (size_t lobe = 0; lobe < 2; ++lobe)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    const float expected = vector["expectedPacked"][lobe][channel].get<float>();
                    const float actual = decision.packed[lobe][channel];
                    if (std::fabs(expected - actual) > 0.0001f)
                    {
                        std::cerr << "Pixel contract vector packed output mismatch: " << id << "\n";
                        passed = false;
                    }
                }
            }
        }
        else if (dispositionIt->second != Disposition::Convertible &&
                 dispositionIt->second != Disposition::ReconstructionRequired)
        {
            for (size_t lobe = 0; lobe < 2; ++lobe)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    if (decision.packed[lobe][channel] != 0.0f)
                    {
                        std::cerr << "Pixel contract vector partial output leaked: " << id << "\n";
                        passed = false;
                    }
                }
            }
        }

        std::cout << "Pixel contract vector case executed: " << id << " -> " << expectedDispositionName << "\n";
    }

    std::cout << "Pixel contract vectors: executed=" << executed << " host-only=" << hostOnly << "\n";
    if (executed != 5 || hostOnly != 3)
    {
        std::cerr << "Unexpected host-only contract vector count.\n";
        passed = false;
    }
    return passed;
}
} // namespace

std::filesystem::path FindPixelContractCorpus(int argc, char** argv)
{
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0')
    {
        return argv[1];
    }
    const char* env = std::getenv("RTPBRSURVEY_NRD_PIXEL_CONTRACT_VECTORS");
    if (env != nullptr && env[0] != '\0')
    {
        return env;
    }
#ifdef RTPBRSURVEY_NRD_PIXEL_CONTRACT_VECTORS_PATH
    if (std::filesystem::exists(RTPBRSURVEY_NRD_PIXEL_CONTRACT_VECTORS_PATH))
    {
        return RTPBRSURVEY_NRD_PIXEL_CONTRACT_VECTORS_PATH;
    }
#endif
    std::filesystem::path current = std::filesystem::current_path();
    for (int level = 0; level < 8 && !current.empty(); ++level)
    {
        std::filesystem::path candidate = current / "Tests" / "PathTracing" / "nrd_pixel_contract_vectors.json";
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
        if (current.has_parent_path() && current.parent_path() != current)
        {
            current = current.parent_path();
        }
        else
        {
            break;
        }
    }
    if (argc > 0 && argv[0] != nullptr)
    {
        std::filesystem::path exe(argv[0]);
        std::filesystem::path base = exe.parent_path();
        for (int level = 0; level < 8 && !base.empty(); ++level)
        {
            std::filesystem::path candidate = base / "Tests" / "PathTracing" / "nrd_pixel_contract_vectors.json";
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
            if (base.has_parent_path() && base.parent_path() != base)
            {
                base = base.parent_path();
            }
            else
            {
                break;
            }
        }
    }
    return {};
}

int main(int argc, char** argv)
{
    using Presentation = Engine::NrdRelaxPresentationStatus;
    bool presentationPassed = true;
    presentationPassed &= Engine::NrdRelaxPresentationStatusText(Presentation::UnsupportedTransmission) ==
        std::string("Native fallback: thin transmission is not supported by NRD");
    presentationPassed &= Engine::NrdRelaxPresentationStatusText(Presentation::UnsupportedAlphaMask) ==
        std::string("Native fallback: alpha MASK is not qualified for NRD");
    for (unsigned flags = 0; flags < 32; ++flags)
    {
        const bool requested = (flags & 1) != 0;
        const bool built = (flags & 2) != 0;
        const bool backend = (flags & 4) != 0;
        const bool compatible = (flags & 8) != 0;
        const bool valid = (flags & 16) != 0;
        const auto expected = !requested    ? Presentation::Native
                              : !built      ? Presentation::NotBuilt
                              : !backend    ? Presentation::BackendUnavailable
                              : !compatible ? Presentation::IncompatibleInputs
                              : !valid      ? Presentation::AwaitingEvaluation
                                            : Presentation::Relax;
        const auto actual = Engine::ResolveNrdRelaxPresentation(requested, built, backend, compatible, valid);
        presentationPassed &= actual == expected && Engine::NrdRelaxPresentationStatusText(actual)[0] != '\0';
    }
    const Engine::NrdRelaxSupportInfo defaults;
    const Engine::NrdRelaxSupportInfo info = Engine::QueryNrdRelaxSupport();
    bool passed = presentationPassed && !defaults.available && !info.available &&
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
    passed &= CheckRadianceGate();
    passed &= CheckPixelGate();
    passed &= CheckPixelContractVectors(FindPixelContractCorpus(argc, argv));
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
