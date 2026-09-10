float LinearizeDeviceDepth(float deviceDepth, float cameraNear, float cameraFar, uint orthographicProjection)
{
    const float depth = saturate(deviceDepth);
    if (orthographicProjection != 0)
    {
        return lerp(cameraNear, cameraFar, depth);
    }

    return cameraNear * cameraFar / max(cameraFar - depth * (cameraFar - cameraNear), 1e-6);
}

float VisualizeDeviceDepth(float deviceDepth,
                           uint mode,
                           float displayNear,
                           float displayFar,
                           float gamma,
                           uint invert,
                           float cameraNear,
                           float cameraFar,
                           uint orthographicProjection)
{
    float value = saturate(deviceDepth);
    if (mode != 0)
    {
        const float viewDepth = LinearizeDeviceDepth(deviceDepth, cameraNear, cameraFar, orthographicProjection);
        if (mode == 1)
        {
            value = saturate((viewDepth - displayNear) / max(displayFar - displayNear, 1e-6));
        }
        else
        {
            const float nearDepth = max(displayNear, 1e-6);
            const float farDepth = max(displayFar, nearDepth + 1e-6);
            value = saturate(log(max(viewDepth, nearDepth) / nearDepth) / log(farDepth / nearDepth));
        }
    }

    value = pow(saturate(value), 1.0 / max(gamma, 1e-3));
    return invert != 0 ? 1.0 - value : value;
}
