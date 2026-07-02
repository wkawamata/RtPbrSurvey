RWTexture2D<float2> g_reflectionRayHit : register(u0);
RaytracingAccelerationStructure g_tlas : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normal : register(t2);
Texture2D<float4> g_pbrParams : register(t3);

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
};

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
        g_reflectionRayHit[pixel] = float2(0.0, 0.0);
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
        g_reflectionRayHit[pixel] = float2(0.0, 0.0);
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
        g_reflectionRayHit[pixel] = float2(query.CommittedRayT(), 1.0);
    }
    else
    {
        g_reflectionRayHit[pixel] = float2(0.0, 0.0);
    }
}
