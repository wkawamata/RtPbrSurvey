// Research-only spatial filter. No temporal history or application setting is added.
float StudyFilteredVisibility(int2 pixel, float3 normal, float3 worldPos)
{
#if STUDY_RADIUS == 0
    return g_diffuseEnvironmentVisibility.Load(int3(pixel, 0));
#else
    uint width, height;
    g_diffuseEnvironmentVisibility.GetDimensions(width, height);
    float value = 0;
    float weightSum = 0;
    for (int y = -STUDY_RADIUS; y <= STUDY_RADIUS; ++y)
    {
        for (int x = -STUDY_RADIUS; x <= STUDY_RADIUS; ++x)
        {
            int2 tap = clamp(pixel + int2(x, y), int2(0, 0), int2(width - 1, height - 1));
            float visibility = g_diffuseEnvironmentVisibility.Load(int3(tap, 0));
            float depth = g_depth.Load(int3(tap, 0));
            float3 tapNormal = g_normal.Load(int3(tap, 0)).xyz;
            if (visibility < 0 || depth >= 1 || dot(tapNormal, tapNormal) < 0.0001)
            {
                continue;
            }
            tapNormal = normalize(tapNormal);
            float2 uv = (float2(tap) + 0.5) / float2(width, height);
            float3 delta = ReconstructWorldPosition(uv, depth) - worldPos;
            float spatialWeight = exp(-float(x * x + y * y) / (0.5 * STUDY_RADIUS * STUDY_RADIUS));
            float normalWeight = pow(saturate(dot(normal, tapNormal)), 64);
            float planeWeight = exp(-abs(dot(delta, normal)) / 0.005);
            float distanceWeight = exp(-dot(delta, delta) / (2 * 0.08 * 0.08));
            float weight = spatialWeight * normalWeight * planeWeight * distanceWeight;
            value += visibility * weight;
            weightSum += weight;
        }
    }
    return weightSum > 0 ? value / weightSum : g_diffuseEnvironmentVisibility.Load(int3(pixel, 0));
#endif
}
