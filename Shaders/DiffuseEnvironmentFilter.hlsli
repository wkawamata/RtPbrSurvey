float SampleDiffuseEnvironmentVisibility(int2 pixel, float3 normal, float3 worldPos)
{
    float center = g_diffuseEnvironmentVisibility.Load(int3(pixel, 0));
    if (diffuseEnvironmentSpatialFilterEnabled < 0.5 || center < 0)
    {
        return center;
    }
    uint width, height;
    g_diffuseEnvironmentVisibility.GetDimensions(width, height);
    float value = 0;
    float weightSum = 0;
    float planeDistance = max(diffuseEnvironmentFilterPlaneDistance, 1e-8);
    float worldRadius = max(diffuseEnvironmentFilterWorldRadius, 1e-8);
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            int2 tap = clamp(pixel + int2(x, y), int2(0, 0), int2(width - 1, height - 1));
            float visibility = g_diffuseEnvironmentVisibility.Load(int3(tap, 0));
            float depth = g_depth.Load(int3(tap, 0));
            float3 tapNormal = g_normal.Load(int3(tap, 0)).xyz;
            if (visibility < 0 || depth >= 1 || !all(isfinite(tapNormal)) || dot(tapNormal, tapNormal) < 0.0001)
            {
                continue;
            }
            tapNormal = normalize(tapNormal);
            float2 uv = (float2(tap) + 0.5) / float2(width, height);
            float3 delta = ReconstructWorldPosition(uv, depth) - worldPos;
            if (!all(isfinite(delta)))
            {
                continue;
            }
            float spatialWeight = exp(-float(x * x + y * y) / 8.0);
            float normalWeight = pow(saturate(dot(normal, tapNormal)), 64);
            float planeWeight = exp(-abs(dot(delta, normal)) / planeDistance);
            float distanceWeight = exp(-dot(delta, delta) / (2 * worldRadius * worldRadius));
            float weight = spatialWeight * normalWeight * planeWeight * distanceWeight;
            value += visibility * weight;
            weightSum += weight;
        }
    }
    return weightSum > 0 ? value / weightSum : center;
}
