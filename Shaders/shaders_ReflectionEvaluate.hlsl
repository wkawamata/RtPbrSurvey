#include "FullscreenTriangle.hlsli"

Texture2D<float4> g_normal : register(t1, space3);
Texture2D<float4> g_pbrParams : register(t4, space3);
Texture2D<float> g_depth : register(t6, space3);
TextureCube<float4> g_environmentMap : register(t0, space5);
TextureCube<float4> g_diffuseIrradianceMap : register(t1, space5);
TextureCube<float4> g_specularPrefilterMap : register(t2, space5);
Texture2D<float2> g_brdfLut : register(t3, space5);
Texture2D<float4> g_reflectionRayHit : register(t0, space6);
Texture2D<float4> g_reflectionRayColor : register(t0, space7);
Texture2D<float4> g_reflectionRayMaterial : register(t0, space8);
Texture2D<float4> g_reflectionRayEmission : register(t0, space10);
SamplerState g_sampler : register(s0);

static const float PI = 3.14159265;
static const float SPECULAR_PREFILTER_MAX_MIP = 5.0;

cbuffer ConstantBuffer : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4x4 invViewProj;
    float3 cameraPosition;
    float constantBufferPadding;
};

cbuffer LightingConstants : register(b2)
{
    float3 lightDirection;
    float iblIntensity;
    float3 lightColor;
    float diffuseIntensity;
    float4 backgroundColor;
    float skyboxEnabled;
    float skyboxPreview;
    float skyboxPreviewExposure;
    float lightPassDebugViewMode;
    float directLightEnabled;
    float diffuseIblEnabled;
    float specularIblEnabled;
    float emissiveEnabled;
    float iblDebugMip;
    float iblDebugExposure;
    float rayTracingSupported;
    float shadowMaskBlurEnabled;
    float reflectionHitOverlayEnabled;
    float reflectionHitOverlayIntensity;
    float reflectionHitOverlayMode;
    float reflectionContributionEnabled;
    float reflectionContributionIntensity;
    float reflectionContributionMaxDistance;
};

FullscreenVSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullscreenTriangleVS(vertexId);
}

float3 DecodeNormalOctahedron(float2 encodedNormal)
{
    float2 f = encodedNormal * 2.0 - 1.0;
    float3 normal = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (normal.z < 0.0)
    {
        float2 signNotZero = float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signNotZero;
    }

    return normalize(normal);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

float DistributionGGX(float ndoth, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.000001);
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

float3 ComputeDirectRadiance(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDir)
{
    float3 lightDir = normalize(lightDirection);
    float3 halfDir = normalize(lightDir + viewDir);
    float ndotl = saturate(dot(normal, lightDir));
    float ndotv = saturate(dot(normal, viewDir));
    float ndoth = saturate(dot(normal, halfDir));
    float vdoth = saturate(dot(viewDir, halfDir));

    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 fresnel = FresnelSchlick(vdoth, f0);
    float distribution = DistributionGGX(ndoth, roughness);
    float geometry = GeometrySmith(ndotv, ndotl, roughness);
    float3 specularBrdf = distribution * geometry * fresnel / max(4.0 * ndotv * ndotl, 0.0001);
    float3 diffuseBrdf = (1.0 - fresnel) * (1.0 - metallic) * albedo / PI;

    return (diffuseBrdf + specularBrdf) * lightColor * diffuseIntensity * ndotl * directLightEnabled;
}

float4 PSMain(FullscreenVSOutput input) : SV_TARGET
{
    float depth = g_depth.Load(int3(input.position.xy, 0));
    if (depth >= 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 reflectionHit = g_reflectionRayHit.Sample(g_sampler, input.uv);
    float3 hitColor = g_reflectionRayColor.Sample(g_sampler, input.uv).rgb;
    float4 hitMaterial = g_reflectionRayMaterial.Sample(g_sampler, input.uv);
    float3 hitEmission = g_reflectionRayEmission.Sample(g_sampler, input.uv).rgb;
    float hitMetallic = saturate(hitMaterial.x);
    float hitRoughness = saturate(hitMaterial.y);
    float hitUnlit = saturate(hitMaterial.z);

    float3 normal = normalize(g_normal.Sample(g_sampler, input.uv).rgb);
    float visibleRoughness = saturate(g_pbrParams.Sample(g_sampler, input.uv).g);
    float3 worldPos = ReconstructWorldPosition(input.uv, depth);
    float3 viewDir = normalize(cameraPosition - worldPos);
    float3 reflectionDir = reflect(-viewDir, normal);

    if (reflectionHit.y <= 0.0)
    {
        float3 environmentRadiance = g_environmentMap.Sample(g_sampler, reflectionDir).rgb * iblIntensity * specularIblEnabled;
        float missStrength = (1.0 - visibleRoughness) * reflectionContributionIntensity;
        return float4(environmentRadiance * missStrength, 1.0);
    }

    float3 hitNormal = DecodeNormalOctahedron(reflectionHit.zw);
    float3 hitViewDir = -reflectionDir;
    float directRoughness = max(hitRoughness, 0.04);
    float3 directRadiance = ComputeDirectRadiance(hitColor, hitMetallic, directRoughness, hitNormal, hitViewDir);
    float3 diffuseIrradiance = g_diffuseIrradianceMap.Sample(g_sampler, hitNormal).rgb;
    float3 diffuseIbl = diffuseIrradiance * hitColor * (1.0 - hitMetallic) * iblIntensity * diffuseIblEnabled / PI;
    float3 hitF0 = lerp(float3(0.04, 0.04, 0.04), hitColor, hitMetallic);
    float specularMip = hitRoughness * SPECULAR_PREFILTER_MAX_MIP;
    float3 hitSpecularDirection = reflect(reflectionDir, hitNormal);
    float hitNdotV = saturate(dot(hitNormal, -reflectionDir));
    float2 hitBrdf = g_brdfLut.Sample(g_sampler, float2(hitNdotV, hitRoughness)).rg;
    float3 specularIbl = g_specularPrefilterMap.SampleLevel(g_sampler, hitSpecularDirection, specularMip).rgb *
                         (hitF0 * hitBrdf.x + hitBrdf.y) * iblIntensity * specularIblEnabled;
    float3 litHitColor = directRadiance + diffuseIbl + specularIbl + hitEmission * emissiveEnabled;
    float3 shadedHitColor = lerp(litHitColor, hitColor + hitEmission * emissiveEnabled, hitUnlit);

    float distanceFade = saturate(1.0 - reflectionHit.x / max(reflectionContributionMaxDistance, 0.001));
    float strength = reflectionHit.y * distanceFade * (1.0 - visibleRoughness) * reflectionContributionIntensity;
    return float4(shadedHitColor * strength, 1.0);
}
