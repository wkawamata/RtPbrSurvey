static const float kEnvironmentPi = 3.14159265358979323846;

uint EnvironmentSampleHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float RadicalInverseEnvironment(uint bits)
{
    return float(reversebits(bits)) * 2.3283064365386963e-10;
}

float3 CosineEnvironmentDirection(uint index, uint count, float3 normal)
{
    float u = (float(index) + 0.5) / float(count);
    float phi = 2.0 * kEnvironmentPi * RadicalInverseEnvironment(index);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (sqrt(u) * cos(phi)) + bitangent * (sqrt(u) * sin(phi)) +
                     normal * sqrt(1.0 - u));
}

float3 ScrambledCosineEnvironmentDirection(uint index, uint count, float3 normal, uint2 pixel)
{
    uint hash = EnvironmentSampleHash(pixel.x + pixel.y * 0x9e3779b9u);
    float2 shift = float2(hash >> 8, EnvironmentSampleHash(hash) >> 8) * (1.0 / 16777216.0);
    float u = frac((float(index) + 0.5) / float(count) + shift.x);
    float phi = 2 * kEnvironmentPi * frac(RadicalInverseEnvironment(index) + shift.y);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (sqrt(u) * cos(phi)) + bitangent * (sqrt(u) * sin(phi)) +
                     normal * sqrt(1 - u));
}
