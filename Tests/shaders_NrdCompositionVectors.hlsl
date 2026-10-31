#include "../Shaders/NrdRelaxComposition.hlsli"
struct Input
{
    float4 diffuse;
    float4 specular;
    float4 diffuseFactor;
    float4 specularFactor;
    float4 stable;
    float4 background;
    float4 nativeColor;
};
struct Output
{
    float4 color;
    float4 diagnostics;
    float4 reserved0;
    float4 reserved1;
};
StructuredBuffer<Input> g_inputs : register(t0);
RWStructuredBuffer<Output> g_outputs : register(u0);
cbuffer InputCount : register(b0) { uint count; };
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= count)
        return;
    Input input = g_inputs[id.x];
    float4 mask = float4(input.diffuse.w, input.specular.w, input.stable.w, input.background.w);
    NrdCompositionResult result = ComposeNrdRadiance(input.diffuse.rgb, input.specular.rgb,
        input.diffuseFactor, input.specularFactor, input.stable.rgb, input.background.rgb, input.nativeColor, mask);
    Output output = (Output)0;
    output.color = result.color;
    output.diagnostics = result.diagnostics;
    g_outputs[id.x] = output;
}
