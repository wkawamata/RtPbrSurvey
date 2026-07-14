static const float PBR_PI = 3.14159265;

struct PbrSurface
{
    float3 albedo;
    float3 normal;
    float3 emissive;
    float metallic;
    float roughness;
    float ambientOcclusion;
    float unlit;
};

struct PbrRadianceComponents
{
    float3 direct;
    float3 diffuseIbl;
    float3 specularIbl;
    float3 emissive;
};

PbrSurface MakePbrSurface(float3 albedo,
                          float3 normal,
                          float3 emissive,
                          float metallic,
                          float roughness,
                          float ambientOcclusion,
                          float unlit)
{
    PbrSurface surface;
    surface.albedo = albedo;
    surface.normal = normal;
    surface.emissive = emissive;
    surface.metallic = saturate(metallic);
    surface.roughness = saturate(roughness);
    surface.ambientOcclusion = saturate(ambientOcclusion);
    surface.unlit = saturate(unlit);
    return surface;
}

float3 EvaluatePbrSurfaceRadiance(PbrSurface surface, PbrRadianceComponents components, float emissiveEnabled)
{
    float3 litRadiance =
        components.direct + components.diffuseIbl + components.specularIbl + components.emissive * emissiveEnabled;
    float3 unlitRadiance = surface.albedo + surface.emissive * emissiveEnabled;
    return lerp(litRadiance, unlitRadiance, surface.unlit);
}

float3 PbrF0(float3 albedo, float metallic)
{
    return lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
}

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

float3 EvaluatePbrDirectLighting(float3 albedo,
                                 float metallic,
                                 float roughness,
                                 float3 normal,
                                 float3 viewDir,
                                 float3 lightDir,
                                 float3 lightRadiance)
{
    float3 halfDir = normalize(lightDir + viewDir);
    float ndotl = saturate(dot(normal, lightDir));
    float ndotv = saturate(dot(normal, viewDir));
    float ndoth = saturate(dot(normal, halfDir));
    float vdoth = saturate(dot(viewDir, halfDir));

    float3 f0 = PbrF0(albedo, metallic);
    float3 fresnel = FresnelSchlick(vdoth, f0);
    float distribution = DistributionGGX(ndoth, roughness);
    float geometry = GeometrySmith(ndotv, ndotl, roughness);
    float3 specularBrdf = distribution * geometry * fresnel / max(4.0 * ndotv * ndotl, 0.0001);
    float3 diffuseBrdf = (1.0 - fresnel) * (1.0 - metallic) * albedo / PBR_PI;

    return (diffuseBrdf + specularBrdf) * lightRadiance * ndotl;
}

float3 EvaluatePbrDiffuseIbl(float3 irradiance, float3 albedo, float metallic, float ambientOcclusion)
{
    return irradiance * albedo * (1.0 - metallic) * ambientOcclusion / PBR_PI;
}

float3 EvaluatePbrSpecularIbl(float3 environmentSpecular,
                              float2 brdf,
                              float3 f0,
                              float roughness,
                              float ndotv,
                              float ambientOcclusion)
{
    float3 specularFresnel = FresnelSchlickRoughness(ndotv, f0, roughness);
    float specularOcclusion = ComputeSpecularOcclusion(ndotv, ambientOcclusion, roughness);
    return environmentSpecular * (specularFresnel * brdf.x + brdf.y) * specularOcclusion;
}

PbrRadianceComponents EvaluatePbrRadianceComponents(PbrSurface surface,
                                                    float3 viewDir,
                                                    float3 lightDir,
                                                    float3 lightRadiance,
                                                    float3 diffuseIrradiance,
                                                    float3 environmentSpecular,
                                                    float2 brdf,
                                                    float ndotv,
                                                    float directScale,
                                                    float diffuseIblScale,
                                                    float specularIblScale)
{
    PbrRadianceComponents components;
    float directRoughness = max(surface.roughness, 0.04);
    float3 f0 = PbrF0(surface.albedo, surface.metallic);

    components.direct =
        EvaluatePbrDirectLighting(
            surface.albedo, surface.metallic, directRoughness, surface.normal, viewDir, lightDir, lightRadiance) *
        directScale;
    components.diffuseIbl =
        EvaluatePbrDiffuseIbl(diffuseIrradiance, surface.albedo, surface.metallic, surface.ambientOcclusion) *
        diffuseIblScale;
    components.specularIbl =
        EvaluatePbrSpecularIbl(environmentSpecular, brdf, f0, surface.roughness, ndotv, surface.ambientOcclusion) *
        specularIblScale;
    components.emissive = surface.emissive;
    return components;
}
