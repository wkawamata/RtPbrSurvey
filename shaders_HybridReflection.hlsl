RWTexture2D<float4> g_reflectionRayHit : register(u0);
RaytracingAccelerationStructure g_tlas : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normal : register(t2);
Texture2D<float4> g_pbrParams : register(t3);
ByteAddressBuffer g_sceneVertices : register(t4);
ByteAddressBuffer g_sceneIndices : register(t5);

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
};

static const uint kSceneVertexStride = 52;
static const uint kSceneVertexNormalOffset = 20;

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

float3 LoadCommittedHitNormal(uint primitiveIndex, float2 barycentric, float3x4 objectToWorld)
{
    uint index0;
    uint index1;
    uint index2;
    LoadPrimitiveVertexIndices(primitiveIndex, index0, index1, index2);

    float bary0 = 1.0 - barycentric.x - barycentric.y;
    float3 objectNormal = normalize(LoadSceneVertexNormal(index0) * bary0 +
                                    LoadSceneVertexNormal(index1) * barycentric.x +
                                    LoadSceneVertexNormal(index2) * barycentric.y);

    float3x3 objectToWorld3x3 = float3x3(objectToWorld[0].xyz, objectToWorld[1].xyz, objectToWorld[2].xyz);
    return normalize(mul(objectNormal, objectToWorld3x3));
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
        float3 hitNormal = LoadCommittedHitNormal(query.CommittedPrimitiveIndex(),
                                                  query.CommittedTriangleBarycentrics(),
                                                  query.CommittedObjectToWorld3x4());
        g_reflectionRayHit[pixel] = float4(query.CommittedRayT(), 1.0, EncodeNormalOctahedron(hitNormal));
    }
    else
    {
        g_reflectionRayHit[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    }
}
