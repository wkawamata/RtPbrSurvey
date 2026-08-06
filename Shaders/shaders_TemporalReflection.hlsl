#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_reflectionEvaluatedRadiance : register(t0, space9);
Texture2D<float4> g_reflectionResolvedRadianceHistory : register(t0, space11);
Texture2D<float2> g_motionVector : register(t3, space3);

cbuffer CameraConstants : register(b0)
{
    float4x4 g_viewProj;
    float4x4 g_prevViewProj;
    float4x4 g_invViewProj;
    float3 g_cameraPosition;
    float g_cameraPad;
    float2 g_motionVectorValueOffset;
    float2 g_motionVectorJitterCancellationNdc;
};

cbuffer TemporalReflectionConstants : register(b5)
{
    uint g_historyValid;
    float g_historyWeight;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    const int3 pixel = int3(input.position.xy, 0);
    float4 current = g_reflectionEvaluatedRadiance.Load(pixel);
    if (g_historyValid != 0)
    {
        uint width;
        uint height;
        g_reflectionEvaluatedRadiance.GetDimensions(width, height);

        const float2 storedMotionNdc = g_motionVector.Load(pixel);
        const float2 rawMotionNdc = storedMotionNdc - g_motionVectorJitterCancellationNdc -
                                    g_motionVectorValueOffset;
        const float2 currentUv = input.position.xy / float2(width, height);
        const float2 historyUv = currentUv + float2(0.5, -0.5) * rawMotionNdc;
        if (all(historyUv >= 0.0) && all(historyUv < 1.0))
        {
            const int2 historyPixel = int2(historyUv * float2(width, height));
            const float4 history = g_reflectionResolvedRadianceHistory.Load(int3(historyPixel, 0));
            current.rgb = lerp(current.rgb, history.rgb, g_historyWeight);
        }
    }
    return current;
}
