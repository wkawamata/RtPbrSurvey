#include "Material.hlsli"

RWTexture2D<float4> g_sceneColor : register(u0);
RWTexture2D<float4> g_accumulation : register(u1);
RWTexture2D<float4> g_normalRoughness : register(u2);
RWTexture2D<float> g_viewZ : register(u3);
RWTexture2D<float2> g_motionVectors : register(u4);
RWTexture2D<float4> g_albedo : register(u5);
RWTexture2D<float4> g_diffuseRadianceHitT : register(u6);
RWTexture2D<float4> g_specularRadianceHitT : register(u7);
RaytracingAccelerationStructure g_tlas : register(t0);
ByteAddressBuffer g_sceneVertices : register(t1);
ByteAddressBuffer g_sceneIndices : register(t2);
ByteAddressBuffer g_instanceData : register(t3);
StructuredBuffer<Material> g_materialData : register(t4);
struct MeshRange
{
    uint firstVertex;
    uint vertexCount;
    uint firstIndex;
    uint indexCount;
};
StructuredBuffer<MeshRange> g_meshRanges : register(t5);
TextureCube<float4> g_environmentMap : register(t6);
Texture2D g_texture[] : register(t0, space8);
SamplerState g_sampler : register(s0);

cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
    float3 cameraPosition;
    float cbPad;
};

cbuffer PathTracingConstants : register(b1)
{
    uint usesIndexedDraw;
    uint vertexCount;
    uint indexCount;
    uint hitNormalSource;
    uint debugOutput;
    uint environmentEnabled;
    uint emissiveEnabled;
    uint samplesPerFrame;
    uint sampleStartIndex;
    uint randomSeed;
    float previousSampleCount;
    float rayTMin;
    float rayTMax;
    uint maxBounces;
    uint directLightingEnabled;
    uint shadowEnabled;
    float normalBias;
    float3 lightDirection;
    float environmentIntensity;
    float3 lightColor;
    float diffuseIntensity;
    uint russianRouletteEnabled;
    uint skyboxEnabled;
    uint environmentSamplingMode;
    float4 backgroundColor;
};

#include "SceneRayQuery.hlsli"
#include "PathTracingSampling.hlsli"
#include "PathTracingDirectLightAdapter.hlsli"

static const uint kInstanceDataPreviousWorldOffset = 64;
static const uint kEnvironmentColumns = 16;
static const uint kEnvironmentRows = 8;
static const uint kEnvironmentCells = kEnvironmentColumns * kEnvironmentRows;
groupshared float g_environmentCdf[129];

float3 EnvironmentCellDirection(uint cell, float2 offset)
{
    const float z = 1.0 - 2.0 * ((float(cell / kEnvironmentColumns) + offset.y) / kEnvironmentRows);
    const float phi = 2.0 * kPathTracingPi * ((float(cell % kEnvironmentColumns) + offset.x) / kEnvironmentColumns);
    const float radius = sqrt(max(0.0, 1.0 - z * z));
    return float3(radius * cos(phi), radius * sin(phi), z);
}

void BuildEnvironmentDistribution(uint groupIndex)
{
    for (uint cell = groupIndex; cell < kEnvironmentCells; cell += 64)
    {
        const float3 value = g_environmentMap.SampleLevel(g_sampler, EnvironmentCellDirection(cell, 0.5), 0).rgb;
        const float weight = PathTracingLuminance(max(value, 0.0));
        g_environmentCdf[cell + 1] = isfinite(weight) ? min(weight, 1e20) : 0.0;
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0)
    {
        float total = 0.0;
        for (uint cell = 0; cell < kEnvironmentCells; ++cell)
        {
            total += g_environmentCdf[cell + 1];
        }
        g_environmentCdf[0] = 0.0;
        float cumulative = 0.0;
        for (uint cell = 0; cell < kEnvironmentCells; ++cell)
        {
            const float probability = total > 0.0 ?
                0.95 * (g_environmentCdf[cell + 1] / total) + 0.05 / kEnvironmentCells :
                1.0 / kEnvironmentCells;
            cumulative += probability;
            g_environmentCdf[cell + 1] = cumulative;
        }
        g_environmentCdf[kEnvironmentCells] = 1.0;
    }
    GroupMemoryBarrierWithGroupSync();
}

float EnvironmentImportancePdf(float3 direction)
{
    float phi = atan2(direction.y, direction.x);
    if (phi < 0.0)
    {
        phi += 2.0 * kPathTracingPi;
    }
    const uint column = min(uint(phi * (kEnvironmentColumns / (2.0 * kPathTracingPi))), kEnvironmentColumns - 1);
    const uint row = min(uint(saturate(0.5 * (1.0 - direction.z)) * kEnvironmentRows), kEnvironmentRows - 1);
    const uint cell = row * kEnvironmentColumns + column;
    return (g_environmentCdf[cell + 1] - g_environmentCdf[cell]) * (kEnvironmentCells / (4.0 * kPathTracingPi));
}

PathTracingLightSample SampleEnvironmentImportance(float3 randomSample)
{
    uint lower = 0;
    uint upper = kEnvironmentCells;
    while (lower + 1 < upper)
    {
        const uint middle = (lower + upper) / 2;
        if (randomSample.x < g_environmentCdf[middle])
        {
            upper = middle;
        }
        else
        {
            lower = middle;
        }
    }
    const float3 direction = EnvironmentCellDirection(lower, randomSample.yz);
    PathTracingLightSample result = MakePathTracingDirectionalLightSample(direction,
        g_environmentMap.SampleLevel(g_sampler, direction, 0).rgb * environmentIntensity, rayTMax, 1.0, 0u);
    result.directionPdf = (g_environmentCdf[lower + 1] - g_environmentCdf[lower]) *
        (kEnvironmentCells / (4.0 * kPathTracingPi));
    result.isDelta = 0u;
    return result;
}

struct PrimarySurfaceData
{
    float4 normalRoughness;
    float viewZ;
    float2 motionVector;
    float4 albedo;
    float hitT;
};

PrimarySurfaceData MakeMissPrimarySurfaceData()
{
    PrimarySurfaceData result;
    result.normalRoughness = float4(0.0, 0.0, 0.0, 1.0);
    result.viewZ = 0.0;
    result.motionVector = float2(0.0, 0.0);
    result.albedo = float4(0.0, 0.0, 0.0, 0.0);
    result.hitT = 0.0;
    return result;
}

float3 TransformObjectPointToPreviousWorld(uint instanceId, float3 objectPosition)
{
    const uint matrixOffset = instanceId * kInstanceDataStride + kInstanceDataPreviousWorldOffset;
    const float4 objectPosition4 = float4(objectPosition, 1.0);
    return float3(dot(objectPosition4, asfloat(g_instanceData.Load4(matrixOffset))),
                  dot(objectPosition4, asfloat(g_instanceData.Load4(matrixOffset + 16))),
                  dot(objectPosition4, asfloat(g_instanceData.Load4(matrixOffset + 32))));
}

float ComputePrimaryViewZ(float3 worldPosition)
{
    float4 farCenter = mul(float4(0.0, 0.0, 1.0, 1.0), invViewProj);
    farCenter.xyz /= farCenter.w;
    const float3 cameraForward = normalize(farCenter.xyz - cameraPosition);
    return max(dot(worldPosition - cameraPosition, cameraForward), 0.0);
}

float2 ComputePrimaryMotionVector(float3 worldPosition, float3 objectPosition, uint instanceId)
{
    const float3 previousWorldPosition =
        TransformObjectPointToPreviousWorld(instanceId, objectPosition);
    const float4 currentClipPosition = mul(float4(worldPosition, 1.0), viewProj);
    const float4 previousClipPosition = mul(float4(previousWorldPosition, 1.0), prevViewProj);
    if (abs(currentClipPosition.w) < 0.000001 || abs(previousClipPosition.w) < 0.000001)
    {
        return float2(0.0, 0.0);
    }
    const float2 currentNdc = currentClipPosition.xy / currentClipPosition.w;
    const float2 previousNdc = previousClipPosition.xy / previousClipPosition.w;
    return previousNdc - currentNdc;
}

uint HashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

uint MakeRandomState(uint2 pixel, uint sampleIndex)
{
    return HashUint(pixel.x ^ HashUint(pixel.y ^ HashUint(sampleIndex) ^ HashUint(randomSeed + 0x9e3779b9u)));
}

float NextRandom(inout uint state)
{
    state = HashUint(state + 0x9e3779b9u);
    return float(state >> 8) * (1.0 / 16777216.0);
}

RayDesc MakePrimaryRay(uint2 pixel, float2 subpixelPosition, uint2 dimensions)
{
    float2 uv = (float2(pixel) + subpixelPosition) / float2(dimensions);
    float2 clipUv = uv * 2.0 - 1.0;
    clipUv.y = -clipUv.y;

    float4 nearPosition4 = mul(float4(clipUv, 0.0, 1.0), invViewProj);
    float4 farPosition4 = mul(float4(clipUv, 1.0, 1.0), invViewProj);
    float3 nearPosition = nearPosition4.xyz / nearPosition4.w;
    float3 farPosition = farPosition4.xyz / farPosition4.w;

    RayDesc ray;
    ray.Origin = nearPosition;
    ray.Direction = normalize(farPosition - nearPosition);
    ray.TMin = rayTMin;
    ray.TMax = rayTMax;
    return ray;
}

float3 SampleEnvironmentLighting(float3 direction)
{
    if (environmentEnabled == 0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    if (environmentSamplingMode == 1 || environmentSamplingMode == 2 || environmentSamplingMode == 5)
    {
        return environmentIntensity.xxx;
    }
    return g_environmentMap.SampleLevel(g_sampler, direction, 0).rgb * environmentIntensity;
}

float3 SamplePrimaryMiss(float3 direction)
{
    if (skyboxEnabled == 0)
    {
        return backgroundColor.rgb;
    }
    return g_environmentMap.SampleLevel(g_sampler, direction, 0).rgb;
}

float TraceShadow(float3 worldPosition, float3 normal, PathTracingLightSample lightSample, uint rayFlags)
{
    if (shadowEnabled == 0)
    {
        return 1.0;
    }

    RayDesc shadowRay;
    shadowRay.Origin = worldPosition + normal * normalBias;
    shadowRay.Direction = lightSample.direction;
    shadowRay.TMin = rayTMin;
    shadowRay.TMax = lightSample.distance;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_tlas, rayFlags, 0xff, shadowRay);
    while (query.Proceed())
    {
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

float3 TracePath(uint2 pixel,
                 inout uint randomState,
                 uint2 dimensions,
                 out PrimarySurfaceData primarySurface,
                 out float3 diffuseRadiance,
                 out float3 specularRadiance)
{
    primarySurface = MakeMissPrimarySurfaceData();
    diffuseRadiance = 0.0;
    specularRadiance = 0.0;
    const float2 subpixelPosition = float2(NextRandom(randomState), NextRandom(randomState));
    RayDesc ray = MakePrimaryRay(pixel, subpixelPosition, dimensions);
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    bool primarySpecularPath = false;
    float previousBsdfPdf = 0.0;
    float previousEnvironmentPdf = 0.0;
    const bool environmentMisEnabled = environmentSamplingMode >= 5 && shadowEnabled != 0;

    [loop] for (uint bounce = 0; bounce < max(maxBounces, 1u); ++bounce)
    {
        RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
        query.TraceRayInline(g_tlas, 0, 0xff, ray);
        while (query.Proceed())
        {
        }

        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        {
            float3 missRadiance = bounce == 0 ? SamplePrimaryMiss(ray.Direction) : SampleEnvironmentLighting(ray.Direction);
            if (bounce > 0 && environmentSamplingMode >= 2)
            {
                missRadiance *= environmentMisEnabled ?
                    PathTracingPowerHeuristic(previousBsdfPdf, previousEnvironmentPdf) : 0.0;
            }
            const float3 contribution = throughput * missRadiance;
            radiance += contribution;
            if (bounce > 0)
            {
                if (primarySpecularPath)
                {
                    specularRadiance += contribution;
                }
                else
                {
                    diffuseRadiance += contribution;
                }
            }
            break;
        }

        uint index0;
        uint index1;
        uint index2;
        const uint primitiveIndex = query.CommittedPrimitiveIndex();
        const float2 barycentric = query.CommittedTriangleBarycentrics();
        const uint instanceId = query.CommittedInstanceID();
        LoadPrimitiveVertexIndices(primitiveIndex, instanceId, index0, index1, index2);

        const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
        float3 geometryNormal = LoadCommittedHitGeometricNormal(index0, index1, index2, objectToWorld);
        float3 vertexNormal =
            LoadCommittedHitVertexNormal(index0, index1, index2, barycentric, objectToWorld);
        const HitMaterialSample hitMaterial =
            LoadCommittedHitMaterialSample(index0, index1, index2, barycentric, instanceId);
        if (dot(geometryNormal, ray.Direction) > 0.0)
        {
            geometryNormal = -geometryNormal;
        }
        if (dot(vertexNormal, geometryNormal) < 0.0)
        {
            vertexNormal = -vertexNormal;
        }
        float3 hitNormal = LoadCommittedHitShadingNormal(index0,
                                                         index1,
                                                         index2,
                                                         barycentric,
                                                         objectToWorld,
                                                         vertexNormal,
                                                         hitMaterial);
        hitNormal = dot(hitNormal, geometryNormal) >= 0.0 ? hitNormal : -hitNormal;
        const float3 hitPosition = ray.Origin + ray.Direction * query.CommittedRayT();

        if (bounce == 0)
        {
            const float barycentric0 = 1.0 - barycentric.x - barycentric.y;
            const float3 objectPosition =
                LoadSceneVertexPosition(index0) * barycentric0 +
                LoadSceneVertexPosition(index1) * barycentric.x +
                LoadSceneVertexPosition(index2) * barycentric.y;
            primarySurface.normalRoughness = float4(hitNormal, hitMaterial.roughness);
            primarySurface.viewZ = ComputePrimaryViewZ(hitPosition);
            primarySurface.motionVector = ComputePrimaryMotionVector(hitPosition, objectPosition, instanceId);
            primarySurface.albedo = float4(hitMaterial.albedo, 1.0);
            primarySurface.hitT = query.CommittedRayT();
        }

        if (bounce == 0 && debugOutput < 3)
        {
            if (debugOutput == 1)
            {
                return hitNormal * 0.5 + 0.5;
            }
            if (debugOutput == 2)
            {
                return hitMaterial.emissive;
            }
            return hitMaterial.albedo + (emissiveEnabled != 0 ? hitMaterial.emissive : 0.0);
        }

        const bool unlit = (hitMaterial.flags & MaterialFlagUnlit) != 0;
        if (unlit)
        {
            const float3 contribution = throughput *
                (hitMaterial.albedo + (emissiveEnabled != 0 ? hitMaterial.emissive : 0.0));
            radiance += contribution;
            if (bounce == 0 || !primarySpecularPath)
            {
                diffuseRadiance += contribution;
            }
            else
            {
                specularRadiance += contribution;
            }
            break;
        }

        if (emissiveEnabled != 0)
        {
            const float3 contribution = throughput * hitMaterial.emissive;
            radiance += contribution;
            if (bounce == 0 || !primarySpecularPath)
            {
                diffuseRadiance += contribution;
            }
            else
            {
                specularRadiance += contribution;
            }
        }

        if (directLightingEnabled != 0)
        {
            // One deterministic candidate per enabled light: each source has selection probability 1.
            [loop] for (uint lightIndex = 0; lightIndex < min(ptLightCount, 16u); ++lightIndex)
            {
                const PathTracingLightSample lightSample = MakePathTracingSceneLightSample(
                    lightIndex, hitPosition, rayTMin, rayTMax);
                const PathTracingDirectLightCandidate candidate = MakePathTracingDirectLightCandidate(
                    lightSample, hitMaterial.albedo, hitMaterial.metallic, hitMaterial.roughness,
                    hitNormal, geometryNormal, -ray.Direction);
                if (candidate.valid == 0)
                {
                    continue;
                }
                const float visibility = TraceShadow(hitPosition, geometryNormal, candidate.lightSample, 0);
                const float3 brdf = candidate.diffuseBrdf + candidate.specularBrdf;
                const float3 lighting = candidate.lightSample.radiance *
                    (candidate.normalDotLight * visibility / candidate.lightSample.selectionPdf);
                const float3 contribution = throughput * brdf * lighting;
                radiance += contribution;
                if (bounce == 0)
                {
                    diffuseRadiance += throughput * candidate.diffuseBrdf * lighting;
                    specularRadiance += throughput * candidate.specularBrdf * lighting;
                }
                else if (primarySpecularPath)
                {
                    specularRadiance += contribution;
                }
                else
                {
                    diffuseRadiance += contribution;
                }
            }
        }

        if (environmentEnabled != 0 && environmentSamplingMode >= 2 && bounce + 1 < max(maxBounces, 1u))
        {
            const float2 environmentRandom = float2(NextRandom(randomState), NextRandom(randomState));
            PathTracingLightSample environmentSample = MakePathTracingConstantEnvironmentSample(
                environmentRandom, environmentIntensity.xxx, rayTMax);
            if (environmentSamplingMode == 3 || environmentSamplingMode == 6)
            {
                environmentSample.radiance = SampleEnvironmentLighting(environmentSample.direction);
            }
            else if (environmentSamplingMode == 4 || environmentSamplingMode == 7)
            {
                environmentSample = SampleEnvironmentImportance(float3(environmentRandom, NextRandom(randomState)));
            }
            const PathTracingDirectLightCandidate environmentCandidate = MakePathTracingDirectLightCandidate(
                environmentSample, hitMaterial.albedo, hitMaterial.metallic, hitMaterial.roughness,
                hitNormal, geometryNormal, -ray.Direction);
            if (environmentCandidate.valid != 0)
            {
                const float visibility = TraceShadow(hitPosition, geometryNormal, environmentSample,
                    RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
                const float lightPdf = environmentSample.selectionPdf * environmentSample.directionPdf;
                const float bsdfPdf = EvaluatePathTracingBsdfPdf(hitMaterial.albedo, hitMaterial.metallic,
                    hitMaterial.roughness, hitNormal, -ray.Direction, environmentSample.direction);
                const float misWeight = environmentMisEnabled ? PathTracingPowerHeuristic(lightPdf, bsdfPdf) : 1.0;
                const float3 lighting = environmentSample.radiance * (environmentCandidate.normalDotLight *
                    visibility * misWeight / lightPdf);
                const float3 diffuseContribution = throughput * environmentCandidate.diffuseBrdf * lighting;
                const float3 specularContribution = throughput * environmentCandidate.specularBrdf * lighting;
                const float3 contribution = diffuseContribution + specularContribution;
                radiance += contribution;
                if (bounce == 0)
                {
                    diffuseRadiance += diffuseContribution;
                    specularRadiance += specularContribution;
                }
                else if (primarySpecularPath)
                {
                    specularRadiance += contribution;
                }
                else
                {
                    diffuseRadiance += contribution;
                }
            }
        }

        if (bounce + 1 >= max(maxBounces, 1u))
        {
            break;
        }

        const float lobeSample = NextRandom(randomState);
        const float2 directionSample = float2(NextRandom(randomState), NextRandom(randomState));
        const PathTracingBsdfSample bsdfSample = SamplePathTracingBsdf(hitMaterial.albedo,
                                                                       hitMaterial.metallic,
                                                                       hitMaterial.roughness,
                                                                       hitNormal,
                                                                       -ray.Direction,
                                                                       lobeSample,
                                                                       directionSample);
        if (bsdfSample.valid == 0 || dot(bsdfSample.direction, geometryNormal) <= 0.0)
        {
            break;
        }

        if (bounce == 0)
        {
            primarySpecularPath = bsdfSample.sampledSpecular != 0;
        }

        throughput *= bsdfSample.weight;
        previousBsdfPdf = bsdfSample.pdf;
        previousEnvironmentPdf = environmentEnabled != 0 && environmentMisEnabled && environmentSamplingMode == 7 ?
            EnvironmentImportancePdf(bsdfSample.direction) : 1.0 / (4.0 * kPathTracingPi);
        if (russianRouletteEnabled != 0 && bounce >= 2)
        {
            const float continuationProbability =
                clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.05, 0.95);
            if (NextRandom(randomState) >= continuationProbability)
            {
                break;
            }
            throughput /= continuationProbability;
        }

        ray.Origin = hitPosition + geometryNormal * normalBias;
        ray.Direction = bsdfSample.direction;
        ray.TMin = rayTMin;
        ray.TMax = rayTMax;
    }
    return radiance;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (environmentEnabled != 0 && (environmentSamplingMode == 4 || environmentSamplingMode == 7))
    {
        BuildEnvironmentDistribution(groupIndex);
    }
    uint2 pixel = dispatchThreadId.xy;
    uint width;
    uint height;
    g_sceneColor.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }

    const uint sampleCount = max(samplesPerFrame, 1u);
    float3 frameRadianceSum = 0.0;
    float3 frameDiffuseRadianceSum = 0.0;
    float3 frameSpecularRadianceSum = 0.0;
    PrimarySurfaceData primarySurface = MakeMissPrimarySurfaceData();
    for (uint sampleOffset = 0; sampleOffset < sampleCount; ++sampleOffset)
    {
        uint randomState = MakeRandomState(pixel, sampleStartIndex + sampleOffset);
        PrimarySurfaceData samplePrimarySurface;
        float3 sampleDiffuseRadiance;
        float3 sampleSpecularRadiance;
        frameRadianceSum += TracePath(pixel,
                                      randomState,
                                      uint2(width, height),
                                      samplePrimarySurface,
                                      sampleDiffuseRadiance,
                                      sampleSpecularRadiance);
        frameDiffuseRadianceSum += sampleDiffuseRadiance;
        frameSpecularRadianceSum += sampleSpecularRadiance;
        if (sampleOffset == 0)
        {
            primarySurface = samplePrimarySurface;
        }
    }

    float3 accumulatedRadiance = frameRadianceSum;
    float totalSampleCount = float(sampleCount);
    accumulatedRadiance += g_accumulation[pixel].rgb;
    totalSampleCount += previousSampleCount;

    g_accumulation[pixel] = float4(accumulatedRadiance, totalSampleCount);
    g_sceneColor[pixel] = float4(accumulatedRadiance / max(totalSampleCount, 1.0), 1.0);
    g_normalRoughness[pixel] = primarySurface.normalRoughness;
    g_viewZ[pixel] = primarySurface.viewZ;
    g_motionVectors[pixel] = primarySurface.motionVector;
    g_albedo[pixel] = primarySurface.albedo;
    g_diffuseRadianceHitT[pixel] =
        float4(frameDiffuseRadianceSum / float(sampleCount), primarySurface.hitT);
    g_specularRadianceHitT[pixel] =
        float4(frameSpecularRadianceSum / float(sampleCount), primarySurface.hitT);
}
