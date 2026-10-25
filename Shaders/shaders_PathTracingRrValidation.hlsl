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
    if (!valid)
    {
        // Preserve the input status category in the existing 8-byte summary.
        // Output validation continues to use bit zero for nonfinite RGB.
        uint category = isfinite(value.x) && value.x >= 1 && value.x <= 6 ? (uint)value.x : 31;
        g_summary.InterlockedOr(0, outputMode != 0 ? 1u : (1u << category), ignored);
    }
    g_summary.InterlockedAdd(4, 1, ignored);
}
