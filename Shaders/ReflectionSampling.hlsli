#ifndef REFLECTION_SAMPLING_HLSLI
#define REFLECTION_SAMPLING_HLSLI

struct RoughReflectionSample
{
    float3 direction;
    float directionalPdf;
    uint valid;
    uint deterministicMirror;
};

uint HashReflectionSample(uint2 pixel, uint frameIndex, uint salt)
{
    uint state = pixel.x * 1664525u + pixel.y * 1013904223u;
    state ^= frameIndex * 747796405u;
    state ^= salt;
    state ^= state >> 16u;
    state *= 0x85ebca6bu;
    state ^= state >> 13u;
    state *= 0xc2b2ae35u;
    state ^= state >> 16u;
    return state;
}

float HashReflectionSample01(uint2 pixel, uint frameIndex, uint salt)
{
    return (float(HashReflectionSample(pixel, frameIndex, salt)) + 0.5) / 4294967296.0;
}

void BuildReflectionTangentFrame(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

RoughReflectionSample SampleRoughReflection(uint2 pixel,
                                            uint frameIndex,
                                            float3 viewDirection,
                                            float3 normal,
                                            float roughness,
                                            float3 mirrorDirection)
{
    RoughReflectionSample result;
    if (roughness <= 0.001)
    {
        result.direction = mirrorDirection;
        result.directionalPdf = 0.0;
        result.valid = 1u;
        result.deterministicMirror = 1u;
        return result;
    }

    float2 xi = float2(HashReflectionSample01(pixel, frameIndex, 0x68bc21ebu),
                       HashReflectionSample01(pixel, frameIndex, 0x02e5be93u));
    float alpha = roughness * roughness;
    float phi = 6.28318530718 * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    float3 tangent;
    float3 bitangent;
    BuildReflectionTangentFrame(normal, tangent, bitangent);
    float3 localHalfVector = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 halfVector = normalize(tangent * localHalfVector.x + bitangent * localHalfVector.y +
                                  normal * localHalfVector.z);
    float3 sampledDirection = normalize(reflect(viewDirection, halfVector));

    float ndoth = saturate(dot(normal, halfVector));
    float vdoth = abs(dot(-viewDirection, halfVector));
    float alphaSquared = alpha * alpha;
    float distributionDenominator = ndoth * ndoth * (alphaSquared - 1.0) + 1.0;
    float distribution = alphaSquared /
        max(3.14159265359 * distributionDenominator * distributionDenominator, 0.000001);
    float halfVectorPdf = distribution * ndoth;
    float directionalPdf = halfVectorPdf / max(4.0 * vdoth, 0.000001);

    result.direction = sampledDirection;
    result.directionalPdf = directionalPdf;
    result.valid = dot(sampledDirection, normal) > 0.0 && isfinite(directionalPdf) && directionalPdf > 0.0 ? 1u : 0u;
    result.deterministicMirror = 0u;
    return result;
}

float3 SampleRoughReflectionDirection(uint2 pixel,
                                      uint frameIndex,
                                      float3 viewDirection,
                                      float3 normal,
                                      float roughness,
                                      float3 mirrorDirection)
{
    RoughReflectionSample sample = SampleRoughReflection(
        pixel, frameIndex, viewDirection, normal, roughness, mirrorDirection);

    // A single bounded sample can fall below the visible surface. Preserve a valid ray with the deterministic
    // mirror direction in the existing approximation path instead of adding an unbounded resampling loop.
    // The explicit estimator path consumes sample.valid and returns zero for an invalid sample instead.
    return sample.valid != 0u ? sample.direction : mirrorDirection;
}

#endif
