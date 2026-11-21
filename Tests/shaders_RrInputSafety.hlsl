#include "../Shaders/RayReconstructionInputSafety.hlsli"

RWStructuredBuffer<RrConvertedInputs> g_results : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint test = id.x % 32;
    RrConvertedInputs value = RrSafePlaceholder(0);
    const float nan = asfloat(0x7fc00000);
    const float inf = asfloat(0x7f800000);
    if (test == 1) value.color.x = -1;
    if (test == 2) value.color.x = inf;
    if (test == 3) value.depthMotionHit.y = nan;
    if (test == 4) value.depthMotionHit.x = 0;
    if (test == 5) value.depthMotionHit.x = 65505;
    if (test == 6) value.normalRoughness.w = -1;
    if (test == 7) value.normalRoughness.xyz = 0;
    if (test == 8) value.normalRoughness.z = 2;
    if (test == 9) value.diffuseAlbedo.x = 1.01;
    if (test == 10) value.specularAlbedo.x = inf;
    if (test == 11) value.depthMotionHit.w = -1;
    if (test == 12) value.depthMotionHit.y = 1.01;
    if (test == 13) value.color.w = 0;
    if (test == 14) value.diffuseAlbedo.w = 0;
    if (test == 15) value.specularAlbedo.w = 0;
    if (test == 16) value.normalRoughness.w = nan;
    if (test == 17) value.depthMotionHit.w = 65505;
    if (test == 18) value.depthMotionHit.x = .0001;
    if (test == 19)
    {
        value.color.xyz = value.specularAlbedo.xyz = 65504;
        value.diffuseAlbedo.xyz = 1;
        value.depthMotionHit = float4(65504, -1, 1, 65504);
    }
    if (test == 20)
    {
        value.normalRoughness.xyz = 0;
        value.depthMotionHit.x = 65504;
    }
    if (test >= 21 && test <= 26) value.diagnostics.x = test - 20;
    if (test == 27) value.color.x = asfloat(0x7f7fffff);
    if (test == 28) value.specularAlbedo.x = -1;
    if (test == 29) value.color.x = -inf;
    if (test == 30) value.normalRoughness.z = nan;
    if (test == 31) value.depthMotionHit.x = -1;
    g_results[id.x] = SanitizeRrInputs(value, test == 20);
}
