#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{

void CreateRootSignature(ID3D12Device* device,
                         UINT textureSrvCount,
                         UINT gbufferSrvCount,
                         Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature);

} // namespace Engine
