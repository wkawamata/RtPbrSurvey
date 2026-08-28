#include "stdafx.h"

#include "ReflectionHdrDiagnosticCapture.h"

#include "../Shared/Error.h"

#include <DirectXPackedVector.h>

namespace Engine
{

bool ReflectionHdrDiagnosticReadback::IsValid() const
{
    return resource != nullptr;
}

void ReflectionHdrDiagnosticReadback::Reset()
{
    resource.Reset();
    layout = {};
    format = DXGI_FORMAT_UNKNOWN;
    roi = {};
}

bool ReflectionHdrDiagnosticCapture::IsReady() const
{
    return evaluatedRadiance.IsValid() && specularEstimate.IsValid() && resolvedRadiance.IsValid() &&
           resolvedSpecularEstimate.IsValid() && specularMoments.IsValid() && visiblePbrParams.IsValid() &&
           specularConfidence.IsValid() && rayHit.IsValid() && motionVector.IsValid();
}

void ReflectionHdrDiagnosticCapture::Reset()
{
    evaluatedRadiance.Reset();
    specularEstimate.Reset();
    resolvedRadiance.Reset();
    resolvedSpecularEstimate.Reset();
    specularMoments.Reset();
    specularConfidence.Reset();
    visiblePbrParams.Reset();
    rayHit.Reset();
    motionVector.Reset();
    samplingFrameIndex = 0;
    temporalFrameIndex = 0;
}

void RecordReflectionHdrDiagnosticReadback(ID3D12GraphicsCommandList* commandList,
                                           ID3D12Device* device,
                                           ID3D12Resource* source,
                                           const ReflectionHdrDiagnosticRoi& roi,
                                           ReflectionHdrDiagnosticReadback& readback)
{
    assert(commandList != nullptr);
    assert(device != nullptr);
    assert(source != nullptr);

    const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
    if (sourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (sourceDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
         sourceDesc.Format != DXGI_FORMAT_R32G32_FLOAT &&
         sourceDesc.Format != DXGI_FORMAT_R16G16_FLOAT &&
         sourceDesc.Format != DXGI_FORMAT_R16_FLOAT &&
         sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) ||
        roi.width == 0 || roi.height == 0 ||
        roi.x >= sourceDesc.Width || roi.y >= sourceDesc.Height || roi.width > sourceDesc.Width - roi.x ||
        roi.height > sourceDesc.Height - roi.y)
    {
        throw std::invalid_argument("Invalid reflection HDR diagnostic ROI or source texture.");
    }

    const D3D12_RESOURCE_DESC roiDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        sourceDesc.Format, roi.width, roi.height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(
        &roiDesc, 0, 1, 0, &readback.layout, &numRows, &rowSizeInBytes, &totalBytes);

    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &CD3DX12_RESOURCE_DESC::Buffer(totalBytes),
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr,
                                                  IID_PPV_ARGS(&readback.resource)));
    readback.roi = roi;
    readback.format = sourceDesc.Format;

    const CD3DX12_TEXTURE_COPY_LOCATION destination(readback.resource.Get(), readback.layout);
    const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(source, 0);
    const D3D12_BOX sourceBox = {roi.x, roi.y, 0, roi.x + roi.width, roi.y + roi.height, 1};
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, &sourceBox);
}

void MapReflectionHdrDiagnosticReadback(ReflectionHdrDiagnosticReadback& readback,
                                        ReflectionHdrDiagnosticMappedReadback& mappedReadback)
{
    assert(readback.IsValid());

    const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(readback.resource->GetDesc().Width)};
    UINT8* data = nullptr;
    ThrowIfFailed(readback.resource->Map(0, &readRange, reinterpret_cast<void**>(&data)));
    mappedReadback.data = data;
    mappedReadback.offset = readback.layout.Offset;
    mappedReadback.rowPitch = readback.layout.Footprint.RowPitch;
    mappedReadback.format = readback.format;
    mappedReadback.roi = readback.roi;
}

void UnmapReflectionHdrDiagnosticReadback(ReflectionHdrDiagnosticReadback& readback)
{
    assert(readback.IsValid());

    const D3D12_RANGE writtenRange = {0, 0};
    readback.resource->Unmap(0, &writtenRange);
}

ReflectionHdrDiagnosticSample ReadReflectionHdrDiagnosticSample(
    const ReflectionHdrDiagnosticMappedReadback& readback,
    UINT localX,
    UINT localY)
{
    assert(readback.data != nullptr);
    assert(localX < readback.roi.width);
    assert(localY < readback.roi.height);

    const UINT8* row = readback.data + readback.offset + static_cast<size_t>(localY) * readback.rowPitch;
    if (readback.format == DXGI_FORMAT_R32G32_FLOAT)
    {
        const float* values = reinterpret_cast<const float*>(row + static_cast<size_t>(localX) * 8);
        return {values[0], values[1], 0.0f, 0.0f};
    }
    if (readback.format == DXGI_FORMAT_R16G16_FLOAT)
    {
        const UINT16* values = reinterpret_cast<const UINT16*>(row + static_cast<size_t>(localX) * 4);
        return {DirectX::PackedVector::XMConvertHalfToFloat(values[0]),
                DirectX::PackedVector::XMConvertHalfToFloat(values[1]),
                0.0f,
                0.0f};
    }
    if (readback.format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        const UINT8* values = row + static_cast<size_t>(localX) * 4;
        constexpr float kUnormScale = 1.0f / 255.0f;
        return {values[0] * kUnormScale,
                values[1] * kUnormScale,
                values[2] * kUnormScale,
                values[3] * kUnormScale};
    }
    if (readback.format == DXGI_FORMAT_R16_FLOAT)
    {
        const UINT16* value = reinterpret_cast<const UINT16*>(row + static_cast<size_t>(localX) * 2);
        return {DirectX::PackedVector::XMConvertHalfToFloat(*value), 0.0f, 0.0f, 0.0f};
    }

    const UINT16* half = reinterpret_cast<const UINT16*>(row + static_cast<size_t>(localX) * 8);
    return {DirectX::PackedVector::XMConvertHalfToFloat(half[0]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[1]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[2]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[3])};
}

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
                                          ReflectionHdrDiagnosticCapture& capture)
{
    capture.Reset();
    RecordReflectionHdrDiagnosticReadback(
        commandList, device, evaluatedRadiance, roi, capture.evaluatedRadiance);
    RecordReflectionHdrDiagnosticReadback(
        commandList, device, specularEstimate, roi, capture.specularEstimate);
    RecordReflectionHdrDiagnosticReadback(commandList, device, resolvedRadiance, roi, capture.resolvedRadiance);
    RecordReflectionHdrDiagnosticReadback(
        commandList, device, resolvedSpecularEstimate, roi, capture.resolvedSpecularEstimate);
    RecordReflectionHdrDiagnosticReadback(commandList, device, specularMoments, roi, capture.specularMoments);
    RecordReflectionHdrDiagnosticReadback(
        commandList, device, specularConfidence, roi, capture.specularConfidence);
    RecordReflectionHdrDiagnosticReadback(commandList, device, visiblePbrParams, roi, capture.visiblePbrParams);
    RecordReflectionHdrDiagnosticReadback(commandList, device, rayHit, roi, capture.rayHit);
    RecordReflectionHdrDiagnosticReadback(commandList, device, motionVector, roi, capture.motionVector);
    capture.samplingFrameIndex = samplingFrameIndex;
    capture.temporalFrameIndex = temporalFrameIndex;
}

namespace
{

std::vector<ReflectionHdrDiagnosticSample> ReadSamples(ReflectionHdrDiagnosticReadback& readback)
{
    ReflectionHdrDiagnosticMappedReadback mapped = {};
    MapReflectionHdrDiagnosticReadback(readback, mapped);
    std::vector<ReflectionHdrDiagnosticSample> samples;
    samples.reserve(static_cast<size_t>(mapped.roi.width) * mapped.roi.height);
    for (UINT y = 0; y < mapped.roi.height; ++y)
    {
        for (UINT x = 0; x < mapped.roi.width; ++x)
        {
            samples.push_back(ReadReflectionHdrDiagnosticSample(mapped, x, y));
        }
    }
    UnmapReflectionHdrDiagnosticReadback(readback);
    return samples;
}

} // namespace

ReflectionHdrDiagnosticFrame ReadReflectionHdrDiagnosticCapture(ReflectionHdrDiagnosticCapture& capture)
{
    assert(capture.IsReady());
    const ReflectionHdrDiagnosticRoi roi = capture.evaluatedRadiance.roi;
    assert(roi.x == capture.specularEstimate.roi.x && roi.y == capture.specularEstimate.roi.y);
    assert(roi.width == capture.specularEstimate.roi.width && roi.height == capture.specularEstimate.roi.height);
    assert(roi.x == capture.resolvedRadiance.roi.x && roi.y == capture.resolvedRadiance.roi.y);
    assert(roi.width == capture.resolvedRadiance.roi.width && roi.height == capture.resolvedRadiance.roi.height);
    assert(roi.x == capture.rayHit.roi.x && roi.y == capture.rayHit.roi.y);
    assert(roi.width == capture.rayHit.roi.width && roi.height == capture.rayHit.roi.height);
    assert(roi.x == capture.resolvedSpecularEstimate.roi.x && roi.y == capture.resolvedSpecularEstimate.roi.y);
    assert(roi.width == capture.resolvedSpecularEstimate.roi.width &&
           roi.height == capture.resolvedSpecularEstimate.roi.height);
    assert(roi.x == capture.specularMoments.roi.x && roi.y == capture.specularMoments.roi.y);
    assert(roi.width == capture.specularMoments.roi.width && roi.height == capture.specularMoments.roi.height);
    assert(roi.x == capture.specularConfidence.roi.x && roi.y == capture.specularConfidence.roi.y);
    assert(roi.width == capture.specularConfidence.roi.width && roi.height == capture.specularConfidence.roi.height);
    assert(roi.x == capture.visiblePbrParams.roi.x && roi.y == capture.visiblePbrParams.roi.y);
    assert(roi.width == capture.visiblePbrParams.roi.width && roi.height == capture.visiblePbrParams.roi.height);
    assert(roi.x == capture.motionVector.roi.x && roi.y == capture.motionVector.roi.y);
    assert(roi.width == capture.motionVector.roi.width && roi.height == capture.motionVector.roi.height);

    ReflectionHdrDiagnosticFrame frame = {};
    frame.roi = roi;
    frame.samplingFrameIndex = capture.samplingFrameIndex;
    frame.temporalFrameIndex = capture.temporalFrameIndex;
    frame.evaluatedRadiance = ReadSamples(capture.evaluatedRadiance);
    frame.specularEstimate = ReadSamples(capture.specularEstimate);
    frame.resolvedRadiance = ReadSamples(capture.resolvedRadiance);
    frame.resolvedSpecularEstimate = ReadSamples(capture.resolvedSpecularEstimate);
    frame.specularMoments = ReadSamples(capture.specularMoments);
    frame.specularConfidence = ReadSamples(capture.specularConfidence);
    frame.visiblePbrParams = ReadSamples(capture.visiblePbrParams);
    frame.rayHit = ReadSamples(capture.rayHit);
    frame.motionVector = ReadSamples(capture.motionVector);
    return frame;
}

} // namespace Engine
