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
    float3 missColor;
};

#include "SceneRayQuery.hlsli"

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

float3 TracePrimaryDiagnostic(uint2 pixel, float2 subpixelPosition, uint2 dimensions)
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

    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    query.TraceRayInline(g_tlas, 0, 0xff, ray);
    query.Proceed();

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return environmentEnabled != 0 ? missColor : float3(0.0, 0.0, 0.0);
    }

    uint index0;
    uint index1;
    uint index2;
    const uint primitiveIndex = query.CommittedPrimitiveIndex();
    const float2 barycentric = query.CommittedTriangleBarycentrics();
    const uint instanceId = query.CommittedInstanceID();
    LoadPrimitiveVertexIndices(primitiveIndex, instanceId, index0, index1, index2);

    const float3 hitNormal = LoadCommittedHitNormal(
        primitiveIndex, barycentric, query.CommittedObjectToWorld3x4(), instanceId);
    const HitMaterialSample hitMaterial =
        LoadCommittedHitMaterialSample(index0, index1, index2, barycentric, instanceId);

    float3 outputColor = hitMaterial.albedo;
    if (debugOutput == 1)
    {
        outputColor = hitNormal * 0.5 + 0.5;
    }
    else if (debugOutput == 2)
    {
        outputColor = hitMaterial.emissive;
    }
    else if (emissiveEnabled != 0)
    {
        outputColor += hitMaterial.emissive;
    }
    return outputColor;
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
        const float2 subpixelPosition = float2(NextRandom(randomState), NextRandom(randomState));
        frameRadianceSum += TracePrimaryDiagnostic(pixel, subpixelPosition, uint2(width, height));
    }

    float3 accumulatedRadiance = frameRadianceSum;
    float totalSampleCount = float(sampleCount);
    accumulatedRadiance += g_accumulation[pixel].rgb;
    totalSampleCount += previousSampleCount;

    g_accumulation[pixel] = float4(accumulatedRadiance, totalSampleCount);
    g_sceneColor[pixel] = float4(accumulatedRadiance / max(totalSampleCount, 1.0), 1.0);
}
