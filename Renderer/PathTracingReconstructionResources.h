#pragma once

#include "PathTracingReconstructionInputs.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{

using PathTracingInputResources = std::array<ID3D12Resource*, kPathTracingInputSignalCount>;

// Captured only after recording a PT dispatch. References preserve allocation
// identity until invalidation. This tracks commands, not GPU completion.
class PathTracingReconstructionProduction
{
public:
    void Invalidate();
    void Capture(const PathTracingReconstructionFrame& frame, const PathTracingInputResources& produced);
    PathTracingInputValidation Validate(uint64_t expectedFrameId, const PathTracingInputResources& current) const;
    const PathTracingReconstructionInputs& Inputs() const;

private:
    bool m_recorded = false;
    PathTracingReconstructionInputs m_inputs;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPathTracingInputSignalCount> m_resources;
    PathTracingInputValidation m_shapeValidation;
};

} // namespace Engine
