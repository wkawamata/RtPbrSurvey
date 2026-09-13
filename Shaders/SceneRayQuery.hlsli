#ifndef RTPBRSURVEY_SCENE_RAY_QUERY_HLSLI
#define RTPBRSURVEY_SCENE_RAY_QUERY_HLSLI

static const uint kSceneVertexStride = 52;
static const uint kSceneVertexPositionOffset = 0;
static const uint kSceneVertexUvOffset = 12;
static const uint kSceneVertexNormalOffset = 20;
static const uint kSceneVertexTangentOffset = 32;
static const uint kSceneVertexMaterialIdOffset = 48;
static const uint kInstanceDataStride = 144;
static const uint kInstanceDataMaterialIdOffset = 128;
static const uint kInstanceDataMeshIdOffset = 132;
static const uint kMaterialFromInstance = 0xffffffff;

struct HitMaterialSample
{
    float3 albedo;
    float3 emissive;
    float metallic;
    float roughness;
    uint flags;
    float2 uv;
    uint materialId;
    uint normalTextureIndex;
};

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

float4 LoadSceneVertexTangent(uint vertexIndex)
{
    if (vertexIndex >= vertexCount)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint tangentOffset = vertexIndex * kSceneVertexStride + kSceneVertexTangentOffset;
    return asfloat(uint4(g_sceneVertices.Load(tangentOffset),
                         g_sceneVertices.Load(tangentOffset + 4),
                         g_sceneVertices.Load(tangentOffset + 8),
                         g_sceneVertices.Load(tangentOffset + 12)));
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

uint LoadInstanceMeshId(uint instanceId)
{
    return g_instanceData.Load(instanceId * kInstanceDataStride + kInstanceDataMeshIdOffset);
}

uint LoadSceneIndex(uint indexIndex)
{
    if (indexIndex >= indexCount)
    {
        return 0;
    }

    return g_sceneIndices.Load(indexIndex * 4);
}

void LoadPrimitiveVertexIndices(uint primitiveIndex,
                                uint instanceId,
                                out uint index0,
                                out uint index1,
                                out uint index2)
{
    MeshRange range = g_meshRanges[LoadInstanceMeshId(instanceId)];
    uint baseIndex = range.firstIndex + primitiveIndex * 3;
    if (range.indexCount != 0)
    {
        index0 = LoadSceneIndex(baseIndex);
        index1 = LoadSceneIndex(baseIndex + 1);
        index2 = LoadSceneIndex(baseIndex + 2);
    }
    else
    {
        uint baseVertex = range.firstVertex + primitiveIndex * 3;
        index0 = baseVertex;
        index1 = baseVertex + 1;
        index2 = baseVertex + 2;
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
    float2 uv = LoadCommittedHitUv(index0, index1, index2, barycentric) * material.uvScale + material.uvOffset;
    float3 color = SrgbToLinear(g_texture[material.albedoTexIndex].SampleLevel(g_sampler, uv, 0).rgb);
    return normalize(color * 2.0 - 1.0);
}

HitMaterialSample LoadCommittedHitMaterialSample(uint index0, uint index1, uint index2, float2 barycentric, uint instanceId)
{
    uint materialId = LoadCommittedHitMaterialId(index0, index1, index2, barycentric, instanceId);
    Material material = g_materialData[materialId];
    float2 uv = LoadCommittedHitUv(index0, index1, index2, barycentric) * material.uvScale + material.uvOffset;
    float4 metallicRoughness = g_texture[material.metallicRoughnessTexIndex].SampleLevel(g_sampler, uv, 0);

    HitMaterialSample result;
    result.albedo = SrgbToLinear(g_texture[material.albedoTexIndex].SampleLevel(g_sampler, uv, 0).rgb);
    result.emissive =
        SrgbToLinear(g_texture[material.emissiveTexIndex].SampleLevel(g_sampler, uv, 0).rgb) * material.emissiveScale;
    result.metallic = saturate(metallicRoughness.b * material.metallicFactor);
    result.roughness = saturate(metallicRoughness.g * material.roughnessFactor);
    result.flags = material.flags;
    result.uv = uv;
    result.materialId = materialId;
    result.normalTextureIndex = material.normalTexIndex;
    return result;
}

float3 LoadCommittedHitShadingNormal(uint index0,
                                     uint index1,
                                     uint index2,
                                     float2 barycentric,
                                     float3x4 objectToWorld,
                                     float3 baseNormal,
                                     HitMaterialSample hitMaterial)
{
    if ((hitMaterial.flags & MaterialFlagHasNormalTexture) == 0)
    {
        return baseNormal;
    }

    const float bary0 = 1.0 - barycentric.x - barycentric.y;
    const float4 tangent0 = LoadSceneVertexTangent(index0);
    const float4 tangent1 = LoadSceneVertexTangent(index1);
    const float4 tangent2 = LoadSceneVertexTangent(index2);
    const float4 objectTangent =
        tangent0 * bary0 + tangent1 * barycentric.x + tangent2 * barycentric.y;
    if (dot(objectTangent.xyz, objectTangent.xyz) < 0.000001)
    {
        return baseNormal;
    }

    float3 worldTangent = TransformObjectNormalToWorld(objectTangent.xyz, objectToWorld);
    worldTangent -= baseNormal * dot(baseNormal, worldTangent);
    if (dot(worldTangent, worldTangent) < 0.000001)
    {
        return baseNormal;
    }
    worldTangent = normalize(worldTangent);
    const float handedness = objectTangent.w >= 0.0 ? 1.0 : -1.0;
    const float3 worldBitangent = cross(baseNormal, worldTangent) * handedness;
    const float3 tangentNormal =
        g_texture[hitMaterial.normalTextureIndex].SampleLevel(g_sampler, hitMaterial.uv, 0).xyz * 2.0 - 1.0;
    return normalize(worldTangent * tangentNormal.x +
                     worldBitangent * tangentNormal.y +
                     baseNormal * tangentNormal.z);
}

float3 GetHitAlbedoPayload(HitMaterialSample hitMaterial)
{
    return hitMaterial.albedo;
}

float4 EncodeHitMaterialPayload(HitMaterialSample hitMaterial)
{
    float unlit = (hitMaterial.flags & MaterialFlagUnlit) ? 1.0 : 0.0;
    return float4(hitMaterial.metallic, hitMaterial.roughness, unlit, 0.0);
}

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

float3 LoadCommittedHitVertexNormal(uint index0,
                                    uint index1,
                                    uint index2,
                                    float2 barycentric,
                                    float3x4 objectToWorld)
{
    const float bary0 = 1.0 - barycentric.x - barycentric.y;
    const float3 objectNormal = normalize(LoadSceneVertexNormal(index0) * bary0 +
                                          LoadSceneVertexNormal(index1) * barycentric.x +
                                          LoadSceneVertexNormal(index2) * barycentric.y);
    return TransformObjectNormalToWorld(objectNormal, objectToWorld);
}

float3 LoadCommittedHitGeometricNormal(uint index0, uint index1, uint index2, float3x4 objectToWorld)
{
    const float3 position0 = TransformObjectPointToWorld(LoadSceneVertexPosition(index0), objectToWorld);
    const float3 position1 = TransformObjectPointToWorld(LoadSceneVertexPosition(index1), objectToWorld);
    const float3 position2 = TransformObjectPointToWorld(LoadSceneVertexPosition(index2), objectToWorld);
    return normalize(cross(position1 - position0, position2 - position0));
}

float3 LoadCommittedHitNormal(uint primitiveIndex, float2 barycentric, float3x4 objectToWorld, uint instanceId)
{
    uint index0;
    uint index1;
    uint index2;
    LoadPrimitiveVertexIndices(primitiveIndex, instanceId, index0, index1, index2);

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
        return LoadCommittedHitGeometricNormal(index0, index1, index2, objectToWorld);
    }

    return LoadCommittedHitVertexNormal(index0, index1, index2, barycentric, objectToWorld);
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

#endif
