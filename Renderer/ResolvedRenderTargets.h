#pragma once

#include <d3d12.h>
#include <optional>
#include <vector>

namespace Engine
{

struct ResolvedRenderTargets
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
    std::optional<D3D12_CPU_DESCRIPTOR_HANDLE> dsv;
    const float* clearColor = nullptr;
};

} // namespace Engine
