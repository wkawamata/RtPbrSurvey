#include "Material.hlsli"

RWTexture2D<float4> g_sceneColor : register(u0);
RWTexture2D<float4> g_accumulation : register(u1);
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
};

#include "SceneRayQuery.hlsli"
#include "PathTracingSampling.hlsli"

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
    return HashUint(pixel.x ^ HashUint(pixel.y ^ HashUint(sampleIndex ^ randomSeed)));
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

float3 SampleEnvironment(float3 direction)
{
    if (environmentEnabled == 0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    return g_environmentMap.SampleLevel(g_sampler, direction, 0).rgb * environmentIntensity;
}

float TraceShadow(float3 worldPosition, float3 normal)
{
    if (shadowEnabled == 0)
    {
        return 1.0;
    }

    RayDesc shadowRay;
    shadowRay.Origin = worldPosition + normal * normalBias;
    shadowRay.Direction = normalize(lightDirection);
    shadowRay.TMin = rayTMin;
    shadowRay.TMax = rayTMax;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_tlas, 0, 0xff, shadowRay);
    while (query.Proceed())
    {
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

float3 TracePath(uint2 pixel, inout uint randomState, uint2 dimensions)
{
    const float2 subpixelPosition = float2(NextRandom(randomState), NextRandom(randomState));
    RayDesc ray = MakePrimaryRay(pixel, subpixelPosition, dimensions);
    float3 radiance = 0.0;
    float3 throughput = 1.0;

    [loop] for (uint bounce = 0; bounce < max(maxBounces, 1u); ++bounce)
    {
        RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
        query.TraceRayInline(g_tlas, 0, 0xff, ray);
        while (query.Proceed())
        {
        }

        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        {
            radiance += throughput * SampleEnvironment(ray.Direction);
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
            radiance += throughput *
                (hitMaterial.albedo + (emissiveEnabled != 0 ? hitMaterial.emissive : 0.0));
            break;
        }

        if (emissiveEnabled != 0)
        {
            radiance += throughput * hitMaterial.emissive;
        }

        const float3 surfaceToLight = normalize(lightDirection);
        const float normalDotLight = saturate(dot(hitNormal, surfaceToLight));
        if (directLightingEnabled != 0 && normalDotLight > 0.0 && dot(geometryNormal, surfaceToLight) > 0.0)
        {
            const float visibility = TraceShadow(hitPosition, geometryNormal);
            const float3 viewDirection = -ray.Direction;
            const float3 brdf = EvaluatePathTracingBrdf(hitMaterial.albedo,
                                                        hitMaterial.metallic,
                                                        hitMaterial.roughness,
                                                        hitNormal,
                                                        viewDirection,
                                                        surfaceToLight);
            radiance += throughput * brdf * lightColor *
                (diffuseIntensity * normalDotLight * visibility);
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

        throughput *= bsdfSample.weight;
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
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
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
    for (uint sampleOffset = 0; sampleOffset < sampleCount; ++sampleOffset)
    {
        uint randomState = MakeRandomState(pixel, sampleStartIndex + sampleOffset);
        frameRadianceSum += TracePath(pixel, randomState, uint2(width, height));
    }

    float3 accumulatedRadiance = frameRadianceSum;
    float totalSampleCount = float(sampleCount);
    accumulatedRadiance += g_accumulation[pixel].rgb;
    totalSampleCount += previousSampleCount;

    g_accumulation[pixel] = float4(accumulatedRadiance, totalSampleCount);
    g_sceneColor[pixel] = float4(accumulatedRadiance / max(totalSampleCount, 1.0), 1.0);
}
