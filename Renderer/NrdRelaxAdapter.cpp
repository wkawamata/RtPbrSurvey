#include "stdafx.h"

#include "NrdRelaxAdapter.h"
#include "Scene/Scene.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace Engine
{
namespace
{
bool CoversSceneRange(const NrdRelaxRayRangeContext& context, float rayTMax)
{
    if (!context.geometrySnapshotMatches || context.bounds == nullptr || !context.bounds->complete)
    {
        return false;
    }
    if (context.currentSnapshotToken != nullptr &&
        (context.expectedSnapshotToken == 0 || *context.currentSnapshotToken != context.expectedSnapshotToken))
    {
        return false;
    }
    double directionLengthSquared = 0.0;
    double maximumDistanceSquared = 0.0;
    for (size_t axis = 0; axis < 3; ++axis)
    {
        const double minimum = context.bounds->minimum[axis];
        const double maximum = context.bounds->maximum[axis];
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum ||
            !std::isfinite(context.origin[axis]) || !std::isfinite(context.direction[axis]))
        {
            return false;
        }
        const double distance =
            (std::max)(std::abs(minimum - context.origin[axis]), std::abs(maximum - context.origin[axis]));
        maximumDistanceSquared += distance * distance;
        directionLengthSquared += double(context.direction[axis]) * context.direction[axis];
    }
    const double directionLength = std::sqrt(directionLengthSquared);
    if (!std::isfinite(directionLength) || std::abs(directionLength - 1.0) > 2e-5)
    {
        return false;
    }
    const double travel = double(rayTMax) * directionLength;
    const double required = std::sqrt(maximumDistanceSquared);
    const double margin = 64.0 * std::numeric_limits<float>::epsilon() * (std::max)({1.0, travel, required});
    return directionLengthSquared > 0.0 && std::isfinite(travel) && travel > required + margin;
}
} // namespace

const SceneMesh& NrdRelaxGeometrySnapshot::CaptureUpload(const SceneMesh& mesh)
{
    Reset();
    auto snapshot = std::make_shared<SceneMesh>();
    snapshot->vertices = mesh.vertices;
    snapshot->indices = mesh.indices;
    snapshot->ranges = mesh.ranges;
    m_uploadedMesh = std::move(snapshot);
    return *m_uploadedMesh;
}

void NrdRelaxGeometrySnapshot::Invalidate()
{
    m_bounds = {};
    if (m_token == std::numeric_limits<uint64_t>::max())
    {
        throw std::overflow_error("NRD geometry snapshot token exhausted.");
    }
    ++m_token;
}

void NrdRelaxGeometrySnapshot::Reset()
{
    Invalidate();
    m_uploadedMesh.reset();
}

void NrdRelaxGeometrySnapshot::RecordTlas(std::span<const InstanceData> instances)
{
    Invalidate();
    if (m_uploadedMesh == nullptr || instances.empty())
    {
        return;
    }
    Scene snapshot;
    snapshot.mesh = m_uploadedMesh.get();
    snapshot.instances.assign(instances.begin(), instances.end());
    m_bounds = BuildNrdRelaxWorldBounds(snapshot);
}

uint64_t NrdRelaxGeometrySnapshot::Token() const
{
    return m_bounds.complete ? m_token : 0;
}

NrdRelaxRayRangeContext NrdRelaxGeometrySnapshot::MakeRangeContext(
    uint64_t token, std::array<float, 3> origin, std::array<float, 3> direction) const
{
    NrdRelaxRayRangeContext context;
    context.bounds = &m_bounds;
    context.origin = origin;
    context.direction = direction;
    context.geometrySnapshotMatches = token != 0 && token == Token();
    context.currentSnapshotToken = &m_token;
    context.expectedSnapshotToken = token;
    return context;
}

NrdRelaxWorldBounds BuildNrdRelaxWorldBounds(const Scene& scene)
{
    NrdRelaxWorldBounds result;
    if (scene.mesh == nullptr || scene.mesh->vertices.empty() || scene.instances.empty())
    {
        return result;
    }
    result.minimum.fill(std::numeric_limits<double>::infinity());
    result.maximum.fill(-std::numeric_limits<double>::infinity());
    const SceneMesh& mesh = *scene.mesh;
    const auto addVertex = [&](size_t index, const InstanceData& instance)
    {
        if (index >= mesh.vertices.size())
        {
            return false;
        }
        const DirectX::XMFLOAT3& position = mesh.vertices[index].position;
        const double coordinates[] = {position.x, position.y, position.z, 1.0};
        for (double coordinate : coordinates)
        {
            if (!std::isfinite(coordinate))
            {
                return false;
            }
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            // TLAS uses the first three rows of the stored transposed world matrix.
            double value = 0.0;
            double magnitude = 0.0;
            for (size_t component = 0; component < 4; ++component)
            {
                const double term = double(instance.world.m[axis][component]) * coordinates[component];
                value += term;
                magnitude += std::abs(term);
            }
            const double padding = 32.0 * std::numeric_limits<float>::epsilon() * (std::max)(1.0, magnitude);
            if (!std::isfinite(value) || !std::isfinite(magnitude) ||
                magnitude + padding > std::numeric_limits<float>::max())
            {
                return false;
            }
            result.minimum[axis] = (std::min)(result.minimum[axis], value - padding);
            result.maximum[axis] = (std::max)(result.maximum[axis], value + padding);
        }
        return true;
    };
    for (const InstanceData& instance : scene.instances)
    {
        for (const auto& row : instance.world.m)
        {
            for (float value : row)
            {
                if (!std::isfinite(value))
                {
                    return {};
                }
            }
        }
        if (instance.world._41 != 0.0f || instance.world._42 != 0.0f || instance.world._43 != 0.0f ||
            instance.world._44 != 1.0f)
        {
            return {};
        }
        SceneMesh::Range range;
        if (mesh.ranges.empty())
        {
            if (instance.meshId != 0 || mesh.vertices.size() > UINT32_MAX || mesh.indices.size() > UINT32_MAX)
            {
                return {};
            }
            range = {0, static_cast<uint32_t>(mesh.vertices.size()), 0, static_cast<uint32_t>(mesh.indices.size())};
        }
        else
        {
            if (instance.meshId >= mesh.ranges.size())
            {
                return {};
            }
            range = mesh.ranges[instance.meshId];
        }
        if (range.vertexCount < 3 || range.firstVertex > mesh.vertices.size() ||
            range.vertexCount > mesh.vertices.size() - range.firstVertex || range.firstIndex > mesh.indices.size() ||
            range.indexCount > mesh.indices.size() - range.firstIndex)
        {
            return {};
        }
        const size_t count = range.indexCount > 0 ? range.indexCount : range.vertexCount;
        if (count % 3 != 0)
        {
            return {};
        }
        for (size_t offset = 0; offset < count; ++offset)
        {
            const size_t index =
                range.indexCount > 0 ? mesh.indices[range.firstIndex + offset] : range.firstVertex + offset;
            if (!addVertex(index, instance))
            {
                return {};
            }
        }
    }
    result.complete = true;
    return result;
}

NrdRelaxRadianceDecision InspectNrdRelaxRadiance(std::array<float, 3> physical,
                                               std::array<float, 3> materialFactor,
                                               bool lobeActive)
{
    NrdRelaxRadianceDecision result;
    for (size_t channel = 0; channel < 3; ++channel)
    {
        if (!std::isfinite(physical[channel]) || physical[channel] < 0 ||
            !std::isfinite(materialFactor[channel]) || materialFactor[channel] <= 0 || materialFactor[channel] > 1 ||
            (!lobeActive && physical[channel] != 0))
        {
            return result;
        }
    }
    if (!lobeActive)
    {
        result.disposition = NrdRelaxRadianceDisposition::InactiveLobe;
        return result;
    }
    std::array<float, 3> demodulated;
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const double value = double(physical[channel]) / materialFactor[channel];
        if (value > 65504)
        {
            result.disposition = NrdRelaxRadianceDisposition::PackingRangeExceeded;
            return result;
        }
        demodulated[channel] = static_cast<float>(value);
    }
    result.disposition = NrdRelaxRadianceDisposition::Packable;
    result.demodulated = demodulated;
    return result;
}

NrdRelaxSupportInfo QueryNrdRelaxSupport()
{
    NrdRelaxSupportInfo info;
    info.targetSdkVersion = "4.17.3";
    info.reason = "NRD RELAX dispatch and PT input conversion are not integrated.";
    return info;
}

NrdRelaxSamplingReadiness QueryNrdRelaxSamplingReadiness(unsigned int samplesPerFrame, bool pixelCenterPrimary)
{
    if (samplesPerFrame != 1)
    {
        return {false, "RELAX prototype requires exactly one primary sample per pixel/frame."};
    }
    if (!pixelCenterPrimary)
    {
        return {false, "RELAX prototype requires pixel-center primary sampling."};
    }
    return {true, "Sampling only is coherent; packing, distances and dispatch remain unvalidated."};
}

NrdRelaxDistanceDecision InspectNrdRelaxDistance(float surfaceDistance,
                                                 float diagnosticState,
                                                 float rawHitT,
                                                 float reserved,
                                                 float rayTMax,
                                                 bool primaryHit,
                                                 bool lobeActive,
                                                 const NrdRelaxRayRangeContext* rangeContext)
{
    NrdRelaxDistanceDecision result;
    if (!std::isfinite(surfaceDistance) || !std::isfinite(diagnosticState) || !std::isfinite(rawHitT) ||
        !std::isfinite(reserved) || !std::isfinite(rayTMax) || surfaceDistance < 0.0f || rawHitT < 0.0f ||
        reserved != 0.0f || rayTMax <= 0.0f)
    {
        return result;
    }
    if (diagnosticState == 0.0f)
    {
        if (surfaceDistance == 0.0f && rawHitT == 0.0f)
        {
            result.disposition = primaryHit ? NrdRelaxDistanceDisposition::TraceRequired
                                            : NrdRelaxDistanceDisposition::ExcludePrimaryMiss;
        }
        return result;
    }
    if (!primaryHit)
    {
        return result;
    }
    if (diagnosticState == 4.0f)
    {
        if (!lobeActive && surfaceDistance == 0.0f && rawHitT == 0.0f)
        {
            result.disposition = NrdRelaxDistanceDisposition::InactiveLobe;
            result.zeroRadianceRequired = true;
        }
        return result;
    }
    if (!lobeActive)
    {
        return result;
    }
    if (diagnosticState == 1.0f && surfaceDistance > 0.0f && rawHitT > 0.0f && rawHitT <= rayTMax)
    {
        if (surfaceDistance > 65504.0f)
        {
            result.disposition = NrdRelaxDistanceDisposition::PackingRangeExceeded;
        }
        else
        {
            result.disposition = NrdRelaxDistanceDisposition::HitCandidate;
            result.hitDistance = surfaceDistance;
        }
    }
    else if (diagnosticState == 2.0f && surfaceDistance == 0.0f && rawHitT == rayTMax)
    {
        result.disposition = NrdRelaxDistanceDisposition::EnvironmentMissPolicyRequired;
        if (rangeContext != nullptr && CoversSceneRange(*rangeContext, rayTMax))
        {
            result.disposition = NrdRelaxDistanceDisposition::EnvironmentMissCandidate;
            result.hitDistance = 65504.0f;
        }
    }
    else if (diagnosticState == 3.0f && surfaceDistance == 0.0f && rawHitT == 0.0f)
    {
        result.disposition = NrdRelaxDistanceDisposition::ReplacementRayRequired;
    }
    return result;
}

} // namespace Engine
