#pragma once

#include <cstdint>
#include <d3d12.h>
#include <vector>
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
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ReflectionHdrDiagnosticRoi roi;

    bool IsValid() const;
    void Reset();
};

struct ReflectionHdrDiagnosticMappedReadback
{
    const UINT8* data = nullptr;
    UINT64 offset = 0;
    UINT rowPitch = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ReflectionHdrDiagnosticRoi roi;
};

struct ReflectionHdrDiagnosticSample
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct ReflectionHdrDiagnosticCapture
{
    ReflectionHdrDiagnosticReadback evaluatedRadiance;
    ReflectionHdrDiagnosticReadback specularEstimate;
    ReflectionHdrDiagnosticReadback resolvedRadiance;
    ReflectionHdrDiagnosticReadback resolvedSpecularEstimate;
    ReflectionHdrDiagnosticReadback specularMoments;
    ReflectionHdrDiagnosticReadback specularConfidence;
    ReflectionHdrDiagnosticReadback visiblePbrParams;
    ReflectionHdrDiagnosticReadback rayHit;
    ReflectionHdrDiagnosticReadback motionVector;
    UINT samplingFrameIndex = 0;
    UINT temporalFrameIndex = 0;

    bool IsReady() const;
    void Reset();
};

struct ReflectionHdrDiagnosticFrame
{
    ReflectionHdrDiagnosticRoi roi;
    UINT samplingFrameIndex = 0;
    UINT temporalFrameIndex = 0;
    std::vector<ReflectionHdrDiagnosticSample> evaluatedRadiance;
    std::vector<ReflectionHdrDiagnosticSample> specularEstimate;
    std::vector<ReflectionHdrDiagnosticSample> resolvedRadiance;
    std::vector<ReflectionHdrDiagnosticSample> resolvedSpecularEstimate;
    std::vector<ReflectionHdrDiagnosticSample> specularMoments;
    std::vector<ReflectionHdrDiagnosticSample> specularConfidence;
    std::vector<ReflectionHdrDiagnosticSample> visiblePbrParams;
    std::vector<ReflectionHdrDiagnosticSample> rayHit;
    std::vector<ReflectionHdrDiagnosticSample> motionVector;
};

// Copies only the requested region. Supported formats cover the linear-HDR signals,
// scalar diagnostics, material payload, and two-channel motion-vector payload.
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
void RecordReflectionHdrDiagnosticCapture(ID3D12GraphicsCommandList* commandList,
                                          ID3D12Device* device,
                                          ID3D12Resource* evaluatedRadiance,
                                          ID3D12Resource* specularEstimate,
                                          ID3D12Resource* resolvedRadiance,
                                          ID3D12Resource* resolvedSpecularEstimate,
                                          ID3D12Resource* specularMoments,
                                          ID3D12Resource* specularConfidence,
                                          ID3D12Resource* visiblePbrParams,
                                          ID3D12Resource* rayHit,
                                          ID3D12Resource* motionVector,
                                          const ReflectionHdrDiagnosticRoi& roi,
                                          UINT samplingFrameIndex,
                                          UINT temporalFrameIndex,
                                          ReflectionHdrDiagnosticCapture& capture);
ReflectionHdrDiagnosticFrame ReadReflectionHdrDiagnosticCapture(ReflectionHdrDiagnosticCapture& capture);

} // namespace Engine
