#ifndef RTPBRSURVEY_EMISSIVE_TRIANGLE_SAMPLING_HLSLI
#define RTPBRSURVEY_EMISSIVE_TRIANGLE_SAMPLING_HLSLI

struct EmissiveTriangleGpu
{
    float3 position0;
    float area;
    float3 position1;
    float cumulativeProbability;
    float3 position2;
    float selectionPdf;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    uint instanceId;
    uint primitiveIndex;
    uint materialId;
    uint padding;
};

float3 EmissiveTriangleBarycentrics(float2 randomSample)
{
    const float root = sqrt(randomSample.x);
    return float3(1.0 - root, root * (1.0 - randomSample.y), root * randomSample.y);
}

uint SelectEmissiveTriangle(StructuredBuffer<EmissiveTriangleGpu> emitters, uint count, float randomSample)
{
    uint lower = 0;
    uint upper = count;
    while (lower < upper)
    {
        const uint middle = lower + (upper - lower) / 2;
        if (randomSample < emitters[middle].cumulativeProbability)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return lower;
}

float EmissiveTriangleSolidAnglePdf(EmissiveTriangleGpu emitter, float3 origin, float3 samplePosition)
{
    const float3 delta = samplePosition - origin;
    const float distanceSquared = dot(delta, delta);
    const float3 crossEdges = cross(emitter.position1 - emitter.position0,
                                    emitter.position2 - emitter.position0);
    const float normalLengthSquared = dot(crossEdges, crossEdges);
    if (distanceSquared <= 0.0 || normalLengthSquared <= 0.0 || emitter.area <= 0.0 ||
        emitter.selectionPdf <= 0.0 || !isfinite(distanceSquared) || !isfinite(normalLengthSquared))
    {
        return 0.0;
    }
    const float cosine = dot(crossEdges * rsqrt(normalLengthSquared), -delta * rsqrt(distanceSquared));
    if (cosine <= 0.0)
    {
        return 0.0;
    }
    const float pdf = emitter.selectionPdf * distanceSquared / (emitter.area * cosine);
    return isfinite(pdf) ? pdf : 0.0;
}

#endif
