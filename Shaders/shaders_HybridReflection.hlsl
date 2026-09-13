#include "Material.hlsli"
#include "ReflectionSampling.hlsli"

RWTexture2D<float4> g_reflectionRayHit : register(u0);
RWTexture2D<float4> g_reflectionRayColor : register(u1);
RWTexture2D<float4> g_reflectionRayMaterial : register(u2);
RWTexture2D<float4> g_reflectionRayEmission : register(u3);
RaytracingAccelerationStructure g_tlas : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normal : register(t2);
Texture2D<float4> g_pbrParams : register(t3);
ByteAddressBuffer g_sceneVertices : register(t4);
ByteAddressBuffer g_sceneIndices : register(t5);
ByteAddressBuffer g_instanceData : register(t6);
StructuredBuffer<Material> g_materialData : register(t7);
struct MeshRange
{
    uint firstVertex;
    uint vertexCount;
    uint firstIndex;
    uint indexCount;
};
StructuredBuffer<MeshRange> g_meshRanges : register(t8);
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

cbuffer ReflectionConstants : register(b1)
{
    float normalBias;
    float rayTMin;
    float rayTMax;
    float maxRoughness;
    float minMetallic;
    uint stochasticSamplingEnabled;
    uint samplingFrameIndex;
    uint usesIndexedDraw;
    uint vertexCount;
    uint indexCount;
    uint hitNormalSource;
};

#include "SceneRayQuery.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    uint width;
    uint height;
    g_reflectionRayHit.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }

    float depth = g_depth.Load(uint3(pixel, 0));
    if (depth >= 1.0)
    {
        g_reflectionRayHit[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayColor[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayMaterial[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayEmission[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(width, height);
    float2 clipUv = uv * 2.0 - 1.0;
    clipUv.y = -clipUv.y;

    float4 clipPosition = float4(clipUv, depth, 1.0);
    float4 worldPosition4 = mul(clipPosition, invViewProj);
    float3 worldPosition = worldPosition4.xyz / worldPosition4.w;

    float3 normal = normalize(g_normal.Load(uint3(pixel, 0)).xyz);
    float4 pbrParams = g_pbrParams.Load(uint3(pixel, 0));
    float roughness = pbrParams.g;
    float metallic = pbrParams.r;

    if (roughness > maxRoughness || metallic < minMetallic)
    {
        g_reflectionRayHit[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayColor[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayMaterial[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayEmission[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 viewDirection = normalize(worldPosition - cameraPosition.xyz);
    float3 reflectionDirection = normalize(reflect(viewDirection, normal));
    if (stochasticSamplingEnabled != 0)
    {
        reflectionDirection = SampleRoughReflectionDirection(
            pixel, samplingFrameIndex, viewDirection, normal, saturate(roughness), reflectionDirection);
    }
    float3 rayOrigin = worldPosition + normal * normalBias;

    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = reflectionDirection;
    ray.TMin = rayTMin;
    ray.TMax = rayTMax;

    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    query.TraceRayInline(g_tlas, 0, 0xff, ray);
    query.Proceed();

    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        uint index0;
        uint index1;
        uint index2;
        const uint primitiveIndex = query.CommittedPrimitiveIndex();
        const float2 barycentric = query.CommittedTriangleBarycentrics();
        const uint instanceId = query.CommittedInstanceID();
        LoadPrimitiveVertexIndices(primitiveIndex, instanceId, index0, index1, index2);

        float3 hitNormal = LoadCommittedHitNormal(query.CommittedPrimitiveIndex(),
                                                  barycentric,
                                                  query.CommittedObjectToWorld3x4(),
                                                  instanceId);
        HitMaterialSample hitMaterial = LoadCommittedHitMaterialSample(index0, index1, index2, barycentric, instanceId);
        g_reflectionRayHit[pixel] = float4(query.CommittedRayT(), 1.0, EncodeNormalOctahedron(hitNormal));
        // ReflectionRayColor is a historical resource name. Keep its payload limited to linear hit albedo.
        g_reflectionRayColor[pixel] = float4(GetHitAlbedoPayload(hitMaterial), 1.0);
        g_reflectionRayMaterial[pixel] = EncodeHitMaterialPayload(hitMaterial);
        g_reflectionRayEmission[pixel] = float4(hitMaterial.emissive, 1.0);
    }
    else
    {
        g_reflectionRayHit[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayColor[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayMaterial[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayEmission[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    }
}
