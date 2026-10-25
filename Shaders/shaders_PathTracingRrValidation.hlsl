Texture2D<float4> g_source : register(t0);
RWByteAddressBuffer g_summary : register(u0);
cbuffer ValidationFrame : register(b0)
{
    uint width;
    uint height;
    uint outputMode;
};
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    float4 value = g_source[id.xy];
    bool valid = outputMode != 0 ? all(isfinite(value.xyz)) : value.x == 0;
    uint ignored;
    if (!valid) g_summary.InterlockedOr(0, 1, ignored);
    g_summary.InterlockedAdd(4, 1, ignored);
}
