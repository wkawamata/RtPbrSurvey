struct Material
{
    uint albedoTexIndex;
    uint metallicRoughnessTexIndex;
    uint emissiveTexIndex;
    uint occlusionTexIndex;
    uint normalTexIndex;
    float roughnessFactor;
    float metallicFactor;
    float occlusionStrength;
    float ambientOcclusionFactor;
    float emissiveScale;
    uint flags;
    float2 uvScale;
    float2 uvOffset;
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
};

static const uint MaterialFlagUnlit = 1u << 0;
static const uint MaterialFlagHasNormalTexture = 1u << 1;
