#include "NrdRelaxInputConversion.hlsli"

Texture2D<float4> g_normal : register(t0);
Texture2D<float4> g_albedo : register(t1);
Texture2D<float4> g_material : register(t2);
Texture2D<float4> g_diffuse : register(t3);
Texture2D<float4> g_specular : register(t4);
Texture2D<float4> g_diffuseDistance : register(t5);
Texture2D<float4> g_specularDistance : register(t6);
Texture2D<float4> g_primary : register(t7);
Texture2D<float4> g_diffuseDirection : register(t8);
Texture2D<float4> g_specularDirection : register(t9);
Texture2D<float> g_viewZ : register(t10);
RWTexture2D<float4> g_outputDiffuse : register(u0);
RWTexture2D<float4> g_outputSpecular : register(u1);
RWTexture2D<float4> g_diagnostics : register(u2);
RWTexture2D<float4> g_outputNormalRoughness : register(u3);
RWTexture2D<float4> g_outputViewZ : register(u4);
RWTexture2D<float4> g_validity : register(u5);
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
RWTexture2D<float> g_tiles : register(u6);
#endif
cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
    float3 cameraPosition;
    float padding;
};
cbuffer Constants : register(b1)
{
    uint width;
    uint height;
    uint hostGate;
    float rayTMax;
    float3 boundsMinimum;
    uint rangeValid;
    float3 boundsMaximum;
    float normalBias;
};

bool CoversScene(float3 position, float4 direction)
{
    if (rangeValid != 1 || any(!isfinite(position)) || any(!isfinite(direction)) || direction.w <= 0 ||
        any(!isfinite(boundsMinimum)) || any(!isfinite(boundsMaximum)) || any(boundsMinimum > boundsMaximum) ||
        !isfinite(normalBias))
        return false;
    float directionLength = length(direction.xyz);
    if (abs(directionLength - 1) > 2e-5)
        return false;
    // Enclose the biased origin without substituting the shading normal for the geometry normal.
    float3 farthest = max(abs(boundsMinimum - position), abs(boundsMaximum - position)) + abs(normalBias);
    float required = length(farthest);
    float travel = rayTMax * directionLength;
    float margin = 128 * 1.192092896e-7 * max(1, max(travel, required));
    return isfinite(required) && isfinite(travel) && travel > required + margin;
}

uint DistanceStatus(float4 d, bool active, float3 position, float4 direction)
{
    if (any(!isfinite(d)) || d.w != 0 || !isfinite(rayTMax) || rayTMax <= 0)
        return 1;
    if (!active)
        return d.x == 0 && d.y == 4 && d.z == 0 ? 0 : 1;
    if (d.y == 1)
        return d.x > 0 && d.x <= 65504 && d.z > 0 && d.z <= rayTMax ? 0 : 1;
    if (d.x != 0)
        return 1;
    if (d.y == 2)
        return d.z != rayTMax ? 1 : CoversScene(position, direction) ? 0 : 6;
    if (d.z != 0)
        return 1;
    if (d.y == 0)
        return 5;
    return d.y == 3 ? 7 : 1;
}

[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint2 pixel = id.xy;
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
    if (all(pixel < (uint2(width, height) + 15) / 16))
        g_tiles[pixel] = 0;
#endif
    g_outputDiffuse[pixel] = 0;
    g_outputSpecular[pixel] = 0;
    g_outputNormalRoughness[pixel] = 0;
    g_outputViewZ[pixel] = float4(1e6, 0, 0, 0);
    g_validity[pixel] = 0;
    g_diagnostics[pixel] = float4(hostGate, 0, 0, 0);
    if (hostGate != 0)
        return;
    float4 primary = g_primary[pixel];
    float4 diffuseDistance = g_diffuseDistance[pixel];
    float4 specularDistance = g_specularDistance[pixel];
    float3 physicalDiffuse = g_diffuse[pixel].xyz;
    float3 physicalSpecular = g_specular[pixel].xyz;
    if (any(!isfinite(primary)))
    {
        g_diagnostics[pixel] = float4(1, 1, 1, 0);
        return;
    }
    if (primary.w == 0)
    {
        uint status = any(diffuseDistance != 0) || any(specularDistance != 0) || any(physicalDiffuse != 0) ||
                              any(physicalSpecular != 0)
                          ? 1
                          : 4;
        g_diagnostics[pixel] = float4(status, status, status, 0);
        return;
    }
    float4 albedo = g_albedo[pixel];
    float4 material = g_material[pixel];
    if (any(!isfinite(primary)) || primary.w != 1 || albedo.w != 1)
    {
        g_diagnostics[pixel] = float4(1, 1, 1, 0);
        return;
    }
    bool diffuseActive = material.w < 1 && any(albedo.xyz > 0);
    uint diffuseGate = DistanceStatus(diffuseDistance, diffuseActive, primary.xyz, g_diffuseDirection[pixel]);
    uint specularGate = DistanceStatus(specularDistance, true, primary.xyz, g_specularDirection[pixel]);
    uint gate = diffuseGate == 1 || specularGate == 1   ? 1
                : diffuseGate == 5 || specularGate == 5 ? 5
                : diffuseGate == 6 || specularGate == 6 ? 6
                                                        : max(diffuseGate, specularGate);
    if (gate != 0 && gate != 7)
    {
        g_diagnostics[pixel] = float4(gate, diffuseGate, specularGate, 0);
        return;
    }
    float2 clip = (float2(pixel) + .5) / float2(width, height) * 2 - 1;
    clip.y = -clip.y;
    float4 nearPoint = mul(float4(clip, 0, 1), invViewProj);
    float4 farPoint = mul(float4(clip, 1, 1), invViewProj);
    float3 view = -normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    float4 normal = g_normal[pixel];
    float viewZ = g_viewZ[pixel];
    float normalLength = length(normal.xyz);
    if (any(!isfinite(normal)) || abs(normalLength - 1) > 2e-3 || normal.w < 0 || normal.w > 1 || !isfinite(viewZ) ||
        viewZ <= 0 || viewZ >= 1e6 || any(!isfinite(view)))
    {
        g_diagnostics[pixel] = float4(11, 0, 0, 0);
        return;
    }
    NrdRelaxConvertedSignals converted = ConvertNrdRelaxSignals(normal.xyz,
                                                                view,
                                                                albedo.xyz,
                                                                material.w,
                                                                material.xyz,
                                                                normal.w,
                                                                physicalDiffuse,
                                                                physicalSpecular,
                                                                diffuseDistance.y == 2 ? 65504 : diffuseDistance.x,
                                                                specularDistance.y == 2 ? 65504 : specularDistance.x);
    g_diagnostics[pixel] =
        float4(converted.conversionReady ? gate : 1, converted.diffuseStatus, converted.specularStatus, 0);
    if (converted.conversionReady)
    {
        g_outputDiffuse[pixel] = converted.diffuse;
        g_outputSpecular[pixel] = converted.specular;
        g_outputNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(normalize(normal.xyz), normal.w, 0);
        g_outputViewZ[pixel] = float4(viewZ, 0, 0, 0);
        g_validity[pixel] = float4(1, gate == 0, gate == 7, 0);
    }
}
