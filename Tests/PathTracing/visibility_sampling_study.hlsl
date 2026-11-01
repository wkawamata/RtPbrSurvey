// Research-only entry point; the application shader and public contract are unchanged.
#define CSMain OriginalCSMain
#include "../../Shaders/shaders_DiffuseEnvironmentVisibility.hlsl"
#undef CSMain

#ifndef STUDY_SAMPLES
#define STUDY_SAMPLES 64
#endif
#ifndef STUDY_SCRAMBLE
#define STUDY_SCRAMBLE 0
#endif
#ifndef STUDY_SEED
#define STUDY_SEED 0
#endif

uint StudyHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float3 StudyDirection(uint index, float3 normal, uint2 pixel)
{
#if STUDY_SCRAMBLE
    uint hash = StudyHash((pixel.x + pixel.y * 0x9e3779b9u) ^ (STUDY_SEED * 0x85ebca6bu));
    float2 shift = float2(hash >> 8, StudyHash(hash) >> 8) * (1.0 / 16777216.0);
    float u = frac((float(index) + 0.5) / STUDY_SAMPLES + shift.x);
    float phi = 2 * kEnvironmentPi * frac(RadicalInverseEnvironment(index) + shift.y);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (sqrt(u) * cos(phi)) + bitangent * (sqrt(u) * sin(phi)) +
                     normal * sqrt(1 - u));
#else
    return CosineEnvironmentDirection(index, STUDY_SAMPLES, normal);
#endif
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    float depth = g_depth.Load(int3(id.xy, 0));
    if (supported == 0 || depth >= 1)
    {
        g_visibility[id.xy] = supported == 0 ? -1 : 1;
        return;
    }
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), invViewProj);
    float3 normal = normalize(g_normal.Load(int3(id.xy, 0)).xyz);
    uint misses = 0;
    for (uint i = 0; i < STUDY_SAMPLES; ++i)
    {
        RayDesc ray;
        ray.Origin = world.xyz / world.w + normal * normalBias;
        ray.Direction = StudyDirection(i, normal, id.xy);
        ray.TMin = rayTMin;
        ray.TMax = traceDistance;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
        query.TraceRayInline(g_tlas, 0, 0xff, ray);
        while (query.Proceed())
        {
        }
        misses += query.CommittedStatus() == COMMITTED_NOTHING ? 1u : 0u;
    }
    g_visibility[id.xy] = float(misses) / STUDY_SAMPLES;
}
