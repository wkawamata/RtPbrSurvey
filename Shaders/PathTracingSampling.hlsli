#ifndef RTPBRSURVEY_PATH_TRACING_SAMPLING_HLSLI
#define RTPBRSURVEY_PATH_TRACING_SAMPLING_HLSLI

static const float kPathTracingPi = 3.14159265359;

struct PathTracingBsdfSample
{
    float3 direction;
    float3 weight;
    float pdf;
    uint valid;
    uint sampledSpecular;
};

float PathTracingLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 PathTracingFresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float PathTracingGgxDistribution(float normalDotHalf, float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPathTracingPi * denominator * denominator, 0.000001);
}

float PathTracingSmithG1(float normalDotDirection, float alpha)
{
    const float normalDotDirectionSquared = normalDotDirection * normalDotDirection;
    const float alphaSquared = alpha * alpha;
    const float denominator = normalDotDirection +
        sqrt(max(alphaSquared + (1.0 - alphaSquared) * normalDotDirectionSquared, 0.0));
    return denominator > 0.0 ? (2.0 * normalDotDirection) / denominator : 0.0;
}

void BuildPathTracingTangentFrame(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

float3 PathTracingDirectionToWorld(float3 localDirection, float3 normal)
{
    float3 tangent;
    float3 bitangent;
    BuildPathTracingTangentFrame(normal, tangent, bitangent);
    return normalize(tangent * localDirection.x + bitangent * localDirection.y + normal * localDirection.z);
}

void EvaluatePathTracingBrdfComponents(float3 albedo,
                                       float metallic,
                                       float roughness,
                                       float3 normal,
                                       float3 viewDirection,
                                       float3 lightDirection,
                                       out float3 diffuse,
                                       out float3 specular)
{
    const float normalDotView = saturate(dot(normal, viewDirection));
    const float normalDotLight = saturate(dot(normal, lightDirection));
    if (normalDotView <= 0.0 || normalDotLight <= 0.0)
    {
        diffuse = float3(0.0, 0.0, 0.0);
        specular = float3(0.0, 0.0, 0.0);
        return;
    }

    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float viewDotHalf = saturate(dot(viewDirection, halfVector));
    const float alpha = max(roughness * roughness, 0.001);
    const float distribution = PathTracingGgxDistribution(normalDotHalf, alpha);
    const float geometry = PathTracingSmithG1(normalDotView, alpha) *
        PathTracingSmithG1(normalDotLight, alpha);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 fresnel = PathTracingFresnelSchlick(viewDotHalf, f0);
    specular = distribution * geometry * fresnel /
        max(4.0 * normalDotView * normalDotLight, 0.000001);
    diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / kPathTracingPi;
}

float3 EvaluatePathTracingBrdf(float3 albedo,
                               float metallic,
                               float roughness,
                               float3 normal,
                               float3 viewDirection,
                               float3 lightDirection)
{
    float3 diffuse;
    float3 specular;
    EvaluatePathTracingBrdfComponents(
        albedo, metallic, roughness, normal, viewDirection, lightDirection, diffuse, specular);
    return diffuse + specular;
}

float PathTracingSpecularProbability(float3 albedo, float metallic, float3 normal, float3 viewDirection)
{
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 fresnel = PathTracingFresnelSchlick(saturate(dot(normal, viewDirection)), f0);
    const float specularWeight = PathTracingLuminance(fresnel);
    const float diffuseWeight = (1.0 - metallic) * PathTracingLuminance(albedo);
    if (diffuseWeight <= 0.0001)
    {
        return 1.0;
    }
    if (specularWeight <= 0.0001)
    {
        return 0.0;
    }
    return clamp(specularWeight / (specularWeight + diffuseWeight), 0.05, 0.95);
}

float3 SamplePathTracingCosineHemisphere(float2 randomSample, float3 normal)
{
    const float radius = sqrt(randomSample.x);
    const float angle = 2.0 * kPathTracingPi * randomSample.y;
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    const float3 localDirection =
        float3(radius * cosine, radius * sine, sqrt(max(0.0, 1.0 - randomSample.x)));
    return PathTracingDirectionToWorld(localDirection, normal);
}

float3 SamplePathTracingGgxHalfVector(float2 randomSample, float roughness, float3 normal)
{
    const float alpha = max(roughness * roughness, 0.001);
    const float phi = 2.0 * kPathTracingPi * randomSample.x;
    const float cosTheta =
        sqrt((1.0 - randomSample.y) / (1.0 + (alpha * alpha - 1.0) * randomSample.y));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float3 localHalfVector = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return PathTracingDirectionToWorld(localHalfVector, normal);
}

float PathTracingSpecularPdf(float3 normal,
                             float3 viewDirection,
                             float3 lightDirection,
                             float roughness)
{
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float viewDotHalf = saturate(dot(viewDirection, halfVector));
    const float alpha = max(roughness * roughness, 0.001);
    return PathTracingGgxDistribution(normalDotHalf, alpha) * normalDotHalf /
        max(4.0 * viewDotHalf, 0.000001);
}

PathTracingBsdfSample SamplePathTracingBsdf(float3 albedo,
                                            float metallic,
                                            float roughness,
                                            float3 normal,
                                            float3 viewDirection,
                                            float lobeSample,
                                            float2 directionSample)
{
    PathTracingBsdfSample result = (PathTracingBsdfSample)0;
    const float specularProbability =
        PathTracingSpecularProbability(albedo, metallic, normal, viewDirection);
    if (lobeSample < specularProbability)
    {
        result.sampledSpecular = 1u;
        const float3 halfVector = SamplePathTracingGgxHalfVector(directionSample, roughness, normal);
        if (dot(viewDirection, halfVector) <= 0.0)
        {
            return result;
        }
        result.direction = normalize(reflect(-viewDirection, halfVector));
    }
    else
    {
        result.direction = SamplePathTracingCosineHemisphere(directionSample, normal);
    }

    const float normalDotLight = saturate(dot(normal, result.direction));
    if (normalDotLight <= 0.0)
    {
        return result;
    }

    const float diffusePdf = normalDotLight / kPathTracingPi;
    const float specularPdf = PathTracingSpecularPdf(normal, viewDirection, result.direction, roughness);
    result.pdf = lerp(diffusePdf, specularPdf, specularProbability);
    if (!isfinite(result.pdf) || result.pdf <= 0.0)
    {
        return result;
    }

    const float3 brdf = EvaluatePathTracingBrdf(
        albedo, metallic, roughness, normal, viewDirection, result.direction);
    result.weight = brdf * normalDotLight / result.pdf;
    result.valid = all(isfinite(result.weight)) ? 1u : 0u;
    return result;
}

#endif
