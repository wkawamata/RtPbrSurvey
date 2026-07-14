static const float PBR_PI = 3.14159265;

float DistributionGGX(float ndoth, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * denom * denom, 0.000001);
}

float GeometrySchlickGGX(float ndotx, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotx / max(ndotx * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(float ndotv, float ndotl, float roughness)
{
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    return f0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0) - f0) *
                    pow(saturate(1.0 - cosTheta), 5.0);
}

float ComputeSpecularOcclusion(float ndotv, float ambientOcclusion, float roughness)
{
    return saturate(pow(ndotv + ambientOcclusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + ambientOcclusion);
}
