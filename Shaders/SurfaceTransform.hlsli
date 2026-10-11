#ifndef RTPBRSURVEY_SURFACE_TRANSFORM_HLSLI
#define RTPBRSURVEY_SURFACE_TRANSFORM_HLSLI

float SurfaceTransformHandedness(float3x3 objectToWorld)
{
    return determinant(objectToWorld) < 0.0 ? -1.0 : 1.0;
}

float3 TransformSurfaceNormal(float3 normal, float3x3 objectToWorld)
{
    const float3x3 cofactors = float3x3(cross(objectToWorld[1], objectToWorld[2]),
                                     cross(objectToWorld[2], objectToWorld[0]),
                                     cross(objectToWorld[0], objectToWorld[1]));
    const float3 transformed = mul(cofactors, normal) * SurfaceTransformHandedness(objectToWorld);
    if (dot(transformed, transformed) < 1e-20)
    {
        return dot(normal, normal) > 1e-20 ? normalize(normal) : float3(0.0, 1.0, 0.0);
    }
    return normalize(transformed);
}

#endif
