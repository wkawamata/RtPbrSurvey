#include "Material.hlsli"

RWTexture2D<float4> g_reflectionRayHit : register(u0);
RWTexture2D<float4> g_reflectionRayColor : register(u1);
RaytracingAccelerationStructure g_tlas : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normal : register(t2);
Texture2D<float4> g_pbrParams : register(t3);
ByteAddressBuffer g_sceneVertices : register(t4);
ByteAddressBuffer g_sceneIndices : register(t5);
ByteAddressBuffer g_instanceData : register(t6);
StructuredBuffer<Material> g_materialData : register(t7);
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
    uint usesIndexedDraw;
    uint vertexCount;
    uint indexCount;
    uint hitNormalSource;
};

static const uint kSceneVertexStride = 52;
static const uint kSceneVertexPositionOffset = 0;
static const uint kSceneVertexUvOffset = 12;
static const uint kSceneVertexNormalOffset = 20;
static const uint kSceneVertexMaterialIdOffset = 48;
static const uint kInstanceDataStride = 144;
static const uint kInstanceDataMaterialIdOffset = 128;
static const uint kMaterialFromInstance = 0xffffffff;

float3 LoadSceneVertexPosition(uint vertexIndex)
{
    if (vertexIndex >= vertexCount)
    {
        return float3(0.0, 0.0, 0.0);
    }

    uint positionOffset = vertexIndex * kSceneVertexStride + kSceneVertexPositionOffset;
    return asfloat(uint3(g_sceneVertices.Load(positionOffset),
                         g_sceneVertices.Load(positionOffset + 4),
                         g_sceneVertices.Load(positionOffset + 8)));
}

float3 LoadSceneVertexNormal(uint vertexIndex)
{
    if (vertexIndex >= vertexCount)
    {
        return float3(0.0, 1.0, 0.0);
    }

    uint normalOffset = vertexIndex * kSceneVertexStride + kSceneVertexNormalOffset;
    return normalize(asfloat(uint3(g_sceneVertices.Load(normalOffset),
                                   g_sceneVertices.Load(normalOffset + 4),
                                   g_sceneVertices.Load(normalOffset + 8))));
}

float2 LoadSceneVertexUv(uint vertexIndex)
{
    if (vertexIndex >= vertexCount)
    {
        return float2(0.0, 0.0);
    }

    uint uvOffset = vertexIndex * kSceneVertexStride + kSceneVertexUvOffset;
    return asfloat(uint2(g_sceneVertices.Load(uvOffset), g_sceneVertices.Load(uvOffset + 4)));
}

uint LoadSceneVertexMaterialId(uint vertexIndex)
{
    if (vertexIndex >= vertexCount)
    {
        return 0;
    }

    return g_sceneVertices.Load(vertexIndex * kSceneVertexStride + kSceneVertexMaterialIdOffset);
}

uint LoadInstanceMaterialId(uint instanceId)
{
    return g_instanceData.Load(instanceId * kInstanceDataStride + kInstanceDataMaterialIdOffset);
}

uint LoadSceneIndex(uint indexIndex)
{
    if (indexIndex >= indexCount)
    {
        return 0;
    }

    return g_sceneIndices.Load(indexIndex * 4);
}

void LoadPrimitiveVertexIndices(uint primitiveIndex, out uint index0, out uint index1, out uint index2)
{
    uint baseIndex = primitiveIndex * 3;
    if (usesIndexedDraw != 0)
    {
        index0 = LoadSceneIndex(baseIndex);
        index1 = LoadSceneIndex(baseIndex + 1);
        index2 = LoadSceneIndex(baseIndex + 2);
    }
    else
    {
        index0 = baseIndex;
        index1 = baseIndex + 1;
        index2 = baseIndex + 2;
    }
}

float3 TransformObjectPointToWorld(float3 objectPosition, float3x4 objectToWorld)
{
    float4 objectPosition4 = float4(objectPosition, 1.0);
    return float3(dot(objectToWorld[0], objectPosition4),
                  dot(objectToWorld[1], objectPosition4),
                  dot(objectToWorld[2], objectPosition4));
}

float3 TransformObjectNormalToWorld(float3 objectNormal, float3x4 objectToWorld)
{
    float3x3 objectToWorld3x3 = float3x3(objectToWorld[0].xyz, objectToWorld[1].xyz, objectToWorld[2].xyz);
    return normalize(mul(objectNormal, objectToWorld3x3));
}

float3 HashMaterialIdToDebugNormal(uint materialId)
{
    uint hash = materialId * 1664525u + 1013904223u;
    float3 color = float3((hash & 255u) / 255.0,
                          ((hash >> 8) & 255u) / 255.0,
                          ((hash >> 16) & 255u) / 255.0);
    return normalize(color * 2.0 - 1.0);
}

float3 MaterialParamsToDebugNormal(uint materialId)
{
    Material material = g_materialData[materialId];
    float3 color = float3(saturate(material.metallicFactor), saturate(material.roughnessFactor), 0.25);
    return normalize(color * 2.0 - 1.0);
}

float3 HitUvToDebugNormal(uint index0, uint index1, uint index2, float2 barycentric)
{
    float bary0 = 1.0 - barycentric.x - barycentric.y;
    float2 uv = LoadSceneVertexUv(index0) * bary0 +
                LoadSceneVertexUv(index1) * barycentric.x +
                LoadSceneVertexUv(index2) * barycentric.y;
    float3 color = float3(frac(uv), 0.25);
    return normalize(color * 2.0 - 1.0);
}

float2 LoadCommittedHitUv(uint index0, uint index1, uint index2, float2 barycentric)
{
    float bary0 = 1.0 - barycentric.x - barycentric.y;
    return LoadSceneVertexUv(index0) * bary0 +
           LoadSceneVertexUv(index1) * barycentric.x +
           LoadSceneVertexUv(index2) * barycentric.y;
}

float3 SrgbToLinear(float3 color)
{
    return pow(saturate(color), 2.2);
}

uint LoadCommittedHitMaterialId(uint index0, uint index1, uint index2, float2 barycentric, uint instanceId);

float3 HitAlbedoToDebugNormal(uint index0, uint index1, uint index2, float2 barycentric, uint instanceId)
{
    uint materialId = LoadCommittedHitMaterialId(index0, index1, index2, barycentric, instanceId);
    Material material = g_materialData[materialId];
    float2 uv = LoadCommittedHitUv(index0, index1, index2, barycentric);
    float3 color = SrgbToLinear(g_texture[material.albedoTexIndex].SampleLevel(g_sampler, uv, 0).rgb);
    return normalize(color * 2.0 - 1.0);
}

float3 LoadCommittedHitAlbedo(uint index0, uint index1, uint index2, float2 barycentric, uint instanceId)
{
    uint materialId = LoadCommittedHitMaterialId(index0, index1, index2, barycentric, instanceId);
    Material material = g_materialData[materialId];
    float2 uv = LoadCommittedHitUv(index0, index1, index2, barycentric);
    return SrgbToLinear(g_texture[material.albedoTexIndex].SampleLevel(g_sampler, uv, 0).rgb);
}

// ReflectionRayColor currently stores linear hit albedo, not fully shaded reflected radiance.

uint LoadCommittedHitMaterialId(uint index0, uint index1, uint index2, float2 barycentric, uint instanceId)
{
    float bary0 = 1.0 - barycentric.x - barycentric.y;
    uint materialId = LoadSceneVertexMaterialId(index0);
    if (barycentric.x > bary0 && barycentric.x >= barycentric.y)
    {
        materialId = LoadSceneVertexMaterialId(index1);
    }
    else if (barycentric.y > bary0 && barycentric.y > barycentric.x)
    {
        materialId = LoadSceneVertexMaterialId(index2);
    }

    return materialId == kMaterialFromInstance ? LoadInstanceMaterialId(instanceId) : materialId;
}

float3 LoadCommittedHitNormal(uint primitiveIndex, float2 barycentric, float3x4 objectToWorld, uint instanceId)
{
    uint index0;
    uint index1;
    uint index2;
    LoadPrimitiveVertexIndices(primitiveIndex, index0, index1, index2);

    float bary0 = 1.0 - barycentric.x - barycentric.y;
    float3 objectNormal = normalize(LoadSceneVertexNormal(index0) * bary0 +
                                    LoadSceneVertexNormal(index1) * barycentric.x +
                                    LoadSceneVertexNormal(index2) * barycentric.y);

    if (hitNormalSource == 2)
    {
        return HashMaterialIdToDebugNormal(LoadCommittedHitMaterialId(index0, index1, index2, barycentric, instanceId));
    }
    if (hitNormalSource == 3)
    {
        return MaterialParamsToDebugNormal(LoadCommittedHitMaterialId(index0, index1, index2, barycentric, instanceId));
    }
    if (hitNormalSource == 4)
    {
        return HitUvToDebugNormal(index0, index1, index2, barycentric);
    }
    if (hitNormalSource == 5)
    {
        return HitAlbedoToDebugNormal(index0, index1, index2, barycentric, instanceId);
    }

    if (hitNormalSource == 1)
    {
        float3 position0 = TransformObjectPointToWorld(LoadSceneVertexPosition(index0), objectToWorld);
        float3 position1 = TransformObjectPointToWorld(LoadSceneVertexPosition(index1), objectToWorld);
        float3 position2 = TransformObjectPointToWorld(LoadSceneVertexPosition(index2), objectToWorld);
        return normalize(cross(position1 - position0, position2 - position0));
    }

    return TransformObjectNormalToWorld(objectNormal, objectToWorld);
}

float2 EncodeNormalOctahedron(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.00001);
    if (normal.z < 0.0)
    {
        float2 signNotZero = float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
        float2 folded = (1.0 - abs(normal.yx)) * signNotZero;
        normal.xy = folded;
    }

    return normal.xy * 0.5 + 0.5;
}

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
        return;
    }

    float3 viewDirection = normalize(worldPosition - cameraPosition.xyz);
    float3 reflectionDirection = normalize(reflect(viewDirection, normal));
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
        LoadPrimitiveVertexIndices(primitiveIndex, index0, index1, index2);

        float3 hitNormal = LoadCommittedHitNormal(query.CommittedPrimitiveIndex(),
                                                  barycentric,
                                                  query.CommittedObjectToWorld3x4(),
                                                  instanceId);
        g_reflectionRayHit[pixel] = float4(query.CommittedRayT(), 1.0, EncodeNormalOctahedron(hitNormal));
        g_reflectionRayColor[pixel] = float4(LoadCommittedHitAlbedo(index0, index1, index2, barycentric, instanceId), 1.0);
    }
    else
    {
        g_reflectionRayHit[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_reflectionRayColor[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    }
}
