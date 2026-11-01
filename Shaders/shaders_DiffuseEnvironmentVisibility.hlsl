#include "DiffuseEnvironmentSampling.hlsli"

RWTexture2D<float> g_visibility : register(u0);
RaytracingAccelerationStructure g_tlas : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normal : register(t2);

cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
    float3 cameraPosition;
    float cameraPadding;
};
cbuffer VisibilityConstants : register(b1)
{
    float normalBias;
    float rayTMin;
    float traceDistance;
    uint sampleCount;
    uint supported;
    uint sampleRotationEnabled;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    if (supported == 0)
    {
        g_visibility[id.xy] = -1; // Diagnostic sentinel, displayed magenta.
        return;
    }
    float depth = g_depth.Load(int3(id.xy, 0));
    if (depth >= 1)
    {
        g_visibility[id.xy] = 1;
        return;
    }
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), invViewProj);
    float3 normal = g_normal.Load(int3(id.xy, 0)).xyz;
    if (!all(isfinite(normal)) || dot(normal, normal) < 0.0001 || !all(isfinite(world)) || abs(world.w) < 1e-8)
    {
        g_visibility[id.xy] = -1;
        return;
    }
    normal = normalize(normal);
    float3 origin = world.xyz / world.w + normal * normalBias;
    uint count = clamp(sampleCount, 1u, 64u);
    uint misses = 0;
    for (uint i = 0; i < count; ++i)
    {
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = sampleRotationEnabled != 0 ?
            ScrambledCosineEnvironmentDirection(i, count, normal, id.xy) :
            CosineEnvironmentDirection(i, count, normal);
        ray.TMin = rayTMin;
        ray.TMax = traceDistance;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
        query.TraceRayInline(g_tlas, 0, 0xff, ray);
        while (query.Proceed())
        {
        }
        misses += query.CommittedStatus() == COMMITTED_NOTHING ? 1u : 0u;
    }
    g_visibility[id.xy] = float(misses) / float(count);
}
