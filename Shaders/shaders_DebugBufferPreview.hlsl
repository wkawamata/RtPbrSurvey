#include "FullscreenTriangle.hlsli"

ByteAddressBuffer g_source : register(t0, space4);

cbuffer DebugBufferPreviewConstants : register(b3)
{
    uint semantic;
    uint channel;
    float exposure;
    float scale;
    float offset;
    uint nearestSampling;
    uint depthMode;
    float depthDisplayNear;
    float depthDisplayFar;
    float depthGamma;
    uint depthInvert;
    float cameraNear;
    float cameraFar;
    uint orthographicProjection;
    uint bufferVisualizationMode;
    uint bufferWidth;
    uint bufferHeight;
    uint bufferRowStrideElements;
    uint bufferElementStride;
    uint bufferComponentOffsetBytes;
    uint bufferComponentCount;
    uint bufferComponentType;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float LoadComponent(uint address)
{
    const uint value = g_source.Load(address);
    if (bufferComponentType == 2)
    {
        return float(value);
    }
    if (bufferComponentType == 3)
    {
        return float(asint(value));
    }
    return asfloat(value);
}

float3 Heatmap(float value)
{
    const float x = saturate(value);
    return saturate(float3(1.5 - abs(4.0 * x - 3.0),
                           1.5 - abs(4.0 * x - 2.0),
                           1.5 - abs(4.0 * x - 1.0)));
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    const uint2 dimensions = uint2(max(bufferWidth, 1), max(bufferHeight, 1));
    const uint2 pixel = min(uint2(input.uv * dimensions), dimensions - 1);
    const uint rowStride = bufferRowStrideElements != 0 ? bufferRowStrideElements : dimensions.x;
    const uint elementIndex = pixel.y * rowStride + pixel.x;
    const uint elementAddress = elementIndex * bufferElementStride + bufferComponentOffsetBytes;

    float4 value = float4(0.0, 0.0, 0.0, 1.0);
    [unroll]
    for (uint component = 0; component < 4; ++component)
    {
        if (component < bufferComponentCount)
        {
            value[component] = LoadComponent(elementAddress + component * 4);
        }
    }
    if (bufferComponentCount == 1)
    {
        value.rgb = value.xxx;
    }
    else if (bufferComponentCount == 2)
    {
        value.b = 0.0;
    }

    if (bufferVisualizationMode == 1)
    {
        float heatmapValue = 0.0;
        if (channel > 0)
        {
            heatmapValue = value[min(channel - 1, 3)];
        }
        else
        {
            [unroll]
            for (uint component = 0; component < 4; ++component)
            {
                heatmapValue += component < bufferComponentCount ? value[component] : 0.0;
            }
            heatmapValue /= max(bufferComponentCount, 1);
        }
        heatmapValue = heatmapValue * exp2(exposure) * scale + offset;
        return float4(Heatmap(heatmapValue), 1.0);
    }
    if (channel > 0)
    {
        const uint component = min(channel - 1, 3);
        const float selectedValue = value[component] * exp2(exposure) * scale + offset;
        return float4(selectedValue, selectedValue, selectedValue, 1.0);
    }

    value.rgb *= exp2(exposure);
    value = value * scale + offset;
    return value;
}
