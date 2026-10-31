#include "stdafx.h"

#include "NrdRelaxAdapter.h"

#include "RELAX_Config.hlsli"

namespace Engine
{
namespace
{
using float4x4 = DirectX::XMFLOAT4X4;
using float4 = DirectX::XMFLOAT4;
using float2 = DirectX::XMFLOAT2;
using uint2 = std::array<unsigned int, 2>;
using int2 = std::array<int, 2>;
using uint = unsigned int;
#define NRD_CONSTANT(type, name) type name;
struct ReconstructionConstants
{
    RELAX_SHARED_CONSTANTS
};
#undef NRD_CONSTANT
static_assert(sizeof(ReconstructionConstants) == 712);
static_assert(offsetof(ReconstructionConstants, gRectSize) == 480);
static_assert(offsetof(ReconstructionConstants, gDenoisingRange) == 588);
} // namespace

std::array<unsigned char, 768> MakeNrdReconstructionConstants(unsigned int width, unsigned int height)
{
    ReconstructionConstants constants = {};
    constants.gRectSize = {static_cast<int>(width), static_cast<int>(height)};
    constants.gRectSizeInv = {width ? 1.0f / width : 0.0f, height ? 1.0f / height : 0.0f};
    constants.gDenoisingRange = 1000.0f;
    constants.gViewZScale = 1.0f;
    std::array<unsigned char, 768> bytes = {};
    memcpy(bytes.data(), &constants, sizeof(constants));
    return bytes;
}
} // namespace Engine
