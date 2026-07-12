#pragma once

#include <cstdint>

namespace Engine
{
namespace RootSignatureLayout
{

enum RootParameterIndex
{
    // Scene texture SRVs used by material shading.
    TextureTable = 0,

    // Scene geometry data shared by GBuffer, depth, and forward/main passes.
    InstanceSrv,
    MaterialSrv,
    CameraConstants,

    // GBuffer debug and lighting pass inputs.
    GBufferSrvBase,
    EnvironmentMap,

    // Lighting pass constants.
    LightConstants,

    // Debug pass controls.
    GBufferDebugConstants,

    // Tone mapping pass inputs and controls.
    ToneMapSceneColor,
    ToneMapConstants,

    // Hybrid reflection debug/composite inputs.
    ReflectionRayHit,
    ReflectionRayColor,
    ReflectionRayMaterial,
    ReflectionRadiance,

    Count
};

static constexpr uint32_t kBaseRegister = 0;

// SRV descriptor tables.
static constexpr uint32_t kTextureSrvSpace = 0;
static constexpr uint32_t kInstanceSrvSpace = 1;
static constexpr uint32_t kMaterialSrvSpace = 2;
static constexpr uint32_t kGBufferSrvSpace = 3;
static constexpr uint32_t kToneMapSceneColorSrvSpace = 4;
static constexpr uint32_t kEnvironmentMapSrvSpace = 5;
static constexpr uint32_t kReflectionRayHitSrvSpace = 6;
static constexpr uint32_t kReflectionRayColorSrvSpace = 7;
static constexpr uint32_t kReflectionRayMaterialSrvSpace = 8;
static constexpr uint32_t kReflectionRadianceSrvSpace = 9;

// Per-frame and per-pass CBVs.
static constexpr uint32_t kCameraCbvRegister = 0;
static constexpr uint32_t kCameraCbvSpace = 0;
static constexpr uint32_t kLightCbvRegister = 2;
static constexpr uint32_t kLightCbvSpace = 0;

// Root constants.
static constexpr uint32_t kGBufferDebugConstantsRegister = 1;
static constexpr uint32_t kGBufferDebugConstantsSpace = 0;
static constexpr uint32_t kGBufferDebugConstantsCount = 3;

static constexpr uint32_t kToneMapConstantsRegister = 3;
static constexpr uint32_t kToneMapConstantsSpace = 0;
static constexpr uint32_t kToneMapConstantsCount = 5;

// Static texture sampler.
static constexpr uint32_t kStaticSamplerRegister = 0;
static constexpr uint32_t kStaticSamplerSpace = 0;

} // namespace RootSignatureLayout
} // namespace Engine
