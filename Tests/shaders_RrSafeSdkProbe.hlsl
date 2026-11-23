// Numeric SDK probe only: synthetic tuples, no ray/radiance correlation claim.
#define CSMain UnusedRawConversionMain
#include "../Shaders/shaders_PathTracingRrConversion.hlsl"
#undef CSMain

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
#ifdef RTPBRSURVEY_RR_PLACEHOLDER_ONLY
    uint test = 0;
#else
    uint test = (id.x + id.y * width) % 4;
#endif
    RrConvertedInputs value = RrSafePlaceholder(0);
    if (test == 1)
    {
        value.color.xyz = value.specularAlbedo.xyz = 65504;
        value.diffuseAlbedo.xyz = 1;
        value.depthMotionHit = float4(65504, -1, 1, 65504);
    }
    if (test == 2)
        value.depthMotionHit.yz = float2(1, -1);
    if (test == 3)
    {
        value.color.xyz = float3(.1, 2, 100);
        value.diffuseAlbedo.xyz = .5;
        value.specularAlbedo.xyz = .04;
        value.normalRoughness = float4(0, 0, -1, .5);
        value.depthMotionHit = float4(64, 0, 0, 1);
    }
    g_color[id.xy] = value.color;
    g_diffuseAlbedo[id.xy] = value.diffuseAlbedo;
    g_specularAlbedo[id.xy] = value.specularAlbedo;
    g_normal[id.xy] = float4(value.normalRoughness.xyz, 0);
    g_roughness[id.xy] = value.normalRoughness.w;
    g_linearDepth[id.xy] = value.depthMotionHit.x;
    g_motion[id.xy] = value.depthMotionHit.yz;
    g_hitDistance[id.xy] = value.depthMotionHit.w;
    g_status[id.xy] = 0;
}
