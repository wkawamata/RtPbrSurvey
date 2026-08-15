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
    roi = {};
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
        sourceDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || roi.width == 0 || roi.height == 0 ||
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
    const UINT16* half = reinterpret_cast<const UINT16*>(row + static_cast<size_t>(localX) * 8);
    return {DirectX::PackedVector::XMConvertHalfToFloat(half[0]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[1]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[2]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[3])};
}

} // namespace Engine
