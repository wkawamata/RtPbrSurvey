#pragma once

#include "NrdRelaxSdkAdapter.h"
#include <d3d12.h>
#include <filesystem>
#include <stdexcept>

namespace Engine
{
bool IsNrdFatalDeviceResult(HRESULT result);

class NrdPreparationError : public std::runtime_error
{
public:
    NrdPreparationError(const std::string& message, uint32_t prepared, uint32_t planned)
        : std::runtime_error(message), preparedDispatches(prepared), plannedDispatches(planned)
    {
    }
    uint32_t preparedDispatches;
    uint32_t plannedDispatches;
};

struct NrdD3D12Inputs
{
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* normalRoughness = nullptr;
    ID3D12Resource* viewZ = nullptr;
    ID3D12Resource* diffuse = nullptr;
    ID3D12Resource* specular = nullptr;
    ID3D12Resource* diffuseOutput = nullptr;
    ID3D12Resource* specularOutput = nullptr;
};

class NrdRelaxD3D12
{
public:
    NrdRelaxD3D12();
    ~NrdRelaxD3D12();
    void Initialize(ID3D12Device* device, const std::filesystem::path& shaderDirectory, uint32_t frameCount,
                    const NrdPrepassSettings& prepass = {}, bool disableAntilag = false);
    const NrdSdkDescription& Description() const;
    // The caller must have completed all GPU work using the old resolution.
    void ReleaseResolutionResources();
    // The frame slot's fence must be complete before descriptors/constants are reused.
    // Inputs enter/leave as non-pixel SRVs; outputs enter/leave as UAVs.
    // NrdPreparationError guarantees no backend commands were recorded. Other failures are fatal.
    uint32_t Record(ID3D12GraphicsCommandList* commands,
                    uint32_t frameSlot,
                    const NrdSdkFrame& frame,
                    const NrdD3D12Inputs& inputs,
                    bool injectInvalidLastDispatch = false);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Engine
