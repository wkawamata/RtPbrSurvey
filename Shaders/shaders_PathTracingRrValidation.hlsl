Texture2D<float4> g_source : register(t0);
RWByteAddressBuffer g_summary : register(u0);
cbuffer ValidationFrame : register(b0)
{
    uint width;
    uint height;
    uint outputMode;
};
// Reduce within each 8x8 group before updating the shared 8-byte summary.
// Every lane participates, including padding lanes in partial edge groups.
groupshared uint g_masks[64];
groupshared uint g_counts[64];
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
    bool inside = id.x < width && id.y < height;
    uint mask = 0;
    if (inside)
    {
        float4 value = g_source[id.xy];
        bool valid = outputMode != 0 ? all(isfinite(value.xyz)) : value.x == 0;
        if (!valid)
        {
            // Preserve the original category bits and output RGB-only check.
            uint category = isfinite(value.x) && value.x >= 1 && value.x <= 6 ? (uint)value.x : 31;
            mask = outputMode != 0 ? 1u : (1u << category);
        }
    }
    g_masks[lane] = mask;
    g_counts[lane] = inside ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride != 0; stride >>= 1)
    {
        if (lane < stride)
        {
            g_masks[lane] |= g_masks[lane + stride];
            g_counts[lane] += g_counts[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0)
    {
        uint ignored;
        if (g_masks[0] != 0)
            g_summary.InterlockedOr(0, g_masks[0], ignored);
        g_summary.InterlockedAdd(4, g_counts[0], ignored);
    }
}
