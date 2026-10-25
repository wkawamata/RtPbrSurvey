#include "RayReconstructionInputConversion.hlsli"

// Standalone tests use uint status. Live diagnostics use a float scalar so the
// existing raw screenshot/preview path can expose the same exact integer codes.
Texture2D<float4> g_frameRadiance : register(t0);
Texture2D<float4> g_normalRoughness : register(t1);
Texture2D<float> g_viewZ : register(t2);
Texture2D<float2> g_motionNdc : register(t3);
Texture2D<float4> g_baseColor : register(t4);
Texture2D<float4> g_f0Metallic : register(t5);
Texture2D<float4> g_primaryPosition : register(t6);
Texture2D<float4> g_specularDistance : register(t7);
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
Texture2D<float4> g_secondaryRayData : register(t8);
Texture2D<float4> g_secondarySurfaceDistance : register(t9);
#endif
RWTexture2D<float4> g_color : register(u0);
RWTexture2D<float4> g_diffuseAlbedo : register(u1);
RWTexture2D<float4> g_specularAlbedo : register(u2);
RWTexture2D<float4> g_normal : register(u3);
RWTexture2D<float> g_roughness : register(u4);
RWTexture2D<float> g_linearDepth : register(u5);
RWTexture2D<float2> g_motion : register(u6);
RWTexture2D<float> g_hitDistance : register(u7);
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
RWTexture2D<float> g_status : register(u8);
#else
RWTexture2D<uint> g_status : register(u8);
#endif
cbuffer ConversionFrame : register(b0)
{
    row_major float4x4 inverseViewProjection;
    uint width;
    uint height;
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    uint hostGate;
    uint actualContinuation;
#endif
};

[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint2 pixel = id.xy;
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    if (hostGate != 0)
    {
        g_color[pixel] = 0;
        g_diffuseAlbedo[pixel] = 0;
        g_specularAlbedo[pixel] = 0;
        g_normal[pixel] = 0;
        g_roughness[pixel] = 0;
        g_linearDepth[pixel] = 0;
        g_motion[pixel] = 0;
        g_hitDistance[pixel] = 0;
        g_status[pixel] = 6; // Host metadata/provenance rejected.
        return;
    }
#endif
    float2 ndc = ((float2(pixel) + .5) / float2(width, height) * 2 - 1) * float2(1, -1);
    float4 nearPoint = mul(float4(ndc, 0, 1), inverseViewProjection);
    float4 farPoint = mul(float4(ndc, 1, 1), inverseViewProjection);
    // Ray direction rather than cameraPosition - hitPosition supports ortho too.
    float3 view = 0;
    if (all(isfinite(nearPoint)) && all(isfinite(farPoint)) && nearPoint.w != 0 && farPoint.w != 0)
        view = normalize(nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w);
    float4 distance = g_specularDistance[pixel];
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    if (actualContinuation != 0)
    {
        float4 ray = g_secondaryRayData[pixel];
        float4 surface = g_secondarySurfaceDistance[pixel];
        distance = float4(surface.x, ray.z, ray.x, 0);
        // First live profile is the validated delta-specular continuation.
        // Do not label arbitrary diffuse proposals as specular hit distance.
        if (ray.y != 2 || g_f0Metallic[pixel].w != 1 || g_normalRoughness[pixel].w != 0 ||
            any(abs(g_motionNdc[pixel]) > 2e-6) || any(!isfinite(surface)) || surface.y != ray.z)
            distance.y = 0;
    }
#endif
    RrConvertedInputs converted = ConvertRrInputs(g_frameRadiance[pixel].xyz,
                                                  g_normalRoughness[pixel],
                                                  g_viewZ[pixel],
                                                  g_motionNdc[pixel],
                                                  g_baseColor[pixel],
                                                  g_f0Metallic[pixel],
                                                  g_primaryPosition[pixel],
                                                  distance,
                                                  view);
    g_color[pixel] = converted.color;
    g_diffuseAlbedo[pixel] = converted.diffuseAlbedo;
    g_specularAlbedo[pixel] = converted.specularAlbedo;
    g_normal[pixel] = float4(converted.normalRoughness.xyz, 0);
    g_roughness[pixel] = converted.normalRoughness.w;
    g_linearDepth[pixel] = converted.depthMotionHit.x;
    g_motion[pixel] = converted.depthMotionHit.yz;
    g_hitDistance[pixel] = converted.depthMotionHit.w;
    g_status[pixel] = (uint)converted.diagnostics.x;
}
