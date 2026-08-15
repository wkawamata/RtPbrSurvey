#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{

struct ReflectionHdrDiagnosticRoi
{
    UINT x = 0;
    UINT y = 0;
    UINT width = 0;
    UINT height = 0;
};

struct ReflectionHdrDiagnosticReadback
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    ReflectionHdrDiagnosticRoi roi;

    bool IsValid() const;
    void Reset();
};

struct ReflectionHdrDiagnosticMappedReadback
{
    const UINT8* data = nullptr;
    UINT64 offset = 0;
    UINT rowPitch = 0;
    ReflectionHdrDiagnosticRoi roi;
};

struct ReflectionHdrDiagnosticSample
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// Copies only the requested region. The source contract is R16G16B16A16_FLOAT;
// this helper intentionally preserves linear HDR values and does not tone map.
void RecordReflectionHdrDiagnosticReadback(ID3D12GraphicsCommandList* commandList,
                                           ID3D12Device* device,
                                           ID3D12Resource* source,
                                           const ReflectionHdrDiagnosticRoi& roi,
                                           ReflectionHdrDiagnosticReadback& readback);
void MapReflectionHdrDiagnosticReadback(ReflectionHdrDiagnosticReadback& readback,
                                        ReflectionHdrDiagnosticMappedReadback& mappedReadback);
void UnmapReflectionHdrDiagnosticReadback(ReflectionHdrDiagnosticReadback& readback);
ReflectionHdrDiagnosticSample ReadReflectionHdrDiagnosticSample(
    const ReflectionHdrDiagnosticMappedReadback& readback,
    UINT localX,
    UINT localY);

} // namespace Engine
