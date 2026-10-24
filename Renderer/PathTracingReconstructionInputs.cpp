#include "PathTracingReconstructionInputs.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool IsUsableMatrix(const std::array<float, 16>& matrix)
{
    if (!std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); }))
    {
        return false;
    }
    // Pivoted elimination rejects singular/uninitialized matrices without a
    // scene-scale-dependent epsilon. Projection semantics are a separate check.
    std::array<std::array<double, 4>, 4> rows = {};
    for (size_t row = 0; row < 4; ++row)
    {
        for (size_t column = 0; column < 4; ++column)
        {
            rows[row][column] = matrix[row * 4 + column];
        }
    }
    for (size_t column = 0; column < 4; ++column)
    {
        size_t pivot = column;
        for (size_t row = column + 1; row < 4; ++row)
        {
            if (std::abs(rows[row][column]) > std::abs(rows[pivot][column]))
            {
                pivot = row;
            }
        }
        if (rows[pivot][column] == 0.0 || !std::isfinite(rows[pivot][column]))
        {
            return false;
        }
        std::swap(rows[column], rows[pivot]);
        for (size_t row = column + 1; row < 4; ++row)
        {
            const double factor = rows[row][column] / rows[column][column];
            for (size_t next = column + 1; next < 4; ++next)
            {
                rows[row][next] -= factor * rows[column][next];
            }
        }
    }
    return true;
}

bool IsAllowedFormat(Engine::PathTracingInputSignal signal, DXGI_FORMAT format)
{
    using Signal = Engine::PathTracingInputSignal;
    switch (signal)
    {
        case Signal::ViewZ:
            return format == DXGI_FORMAT_R32_FLOAT;
        case Signal::MotionVectors:
            return format == DXGI_FORMAT_R16G16_FLOAT || format == DXGI_FORMAT_R32G32_FLOAT;
        case Signal::PrimaryPosition:
        case Signal::DiffuseDirection:
        case Signal::SpecularDirection:
        case Signal::DiffuseDistance:
        case Signal::SpecularDistance:
        case Signal::PrimaryContinuation:
        case Signal::SecondaryRayData:
        case Signal::SecondarySurfaceDistance:
            return format == DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Signal::Count:
            return false;
        default:
            return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
}
} // namespace

namespace Engine
{

PathTracingInputValidation ValidatePathTracingReconstructionInputs(const PathTracingReconstructionInputs& inputs)
{
    using Issue = PathTracingInputIssue;
    using Signal = PathTracingInputSignal;
    const PathTracingReconstructionFrame& frame = inputs.frame;
    if (frame.renderWidth == 0 || frame.renderHeight == 0 || frame.outputWidth == 0 || frame.outputHeight == 0)
    {
        return {Issue::InvalidDimensions};
    }
    if (frame.samplesPerFrame != 1 || !frame.pixelCenterPrimary || frame.samplingSignature == 0 ||
        frame.sampleStartIndex == std::numeric_limits<uint32_t>::max() || frame.jitterPixels[0] != 0.0f ||
        frame.jitterPixels[1] != 0.0f)
    {
        return {Issue::UnsupportedSampling};
    }
    if (frame.preExposure != 1.0f || frame.exposureScale != 1.0f)
    {
        return {Issue::UnsupportedExposure};
    }
    if (!IsUsableMatrix(frame.worldToView) || !IsUsableMatrix(frame.viewToClip) ||
        !IsUsableMatrix(frame.previousViewProjection))
    {
        return {Issue::InvalidCameraMatrix};
    }

    for (size_t i = 0; i < inputs.signals.size(); ++i)
    {
        const Signal signal = static_cast<Signal>(i);
        const PathTracingInputMetadata& metadata = inputs.signals[i];
        if (!metadata.present)
        {
            // Physical/stable/background/distance signals are optional at this
            // layer; a backend must additionally require its own capabilities.
            if (i <= static_cast<size_t>(Signal::MaterialF0Metallic))
            {
                return {Issue::MissingRequiredSignal, signal};
            }
            continue;
        }
        if (!IsAllowedFormat(signal, metadata.format))
        {
            return {Issue::InvalidFormat, signal};
        }
        if (metadata.width != frame.renderWidth || metadata.height != frame.renderHeight)
        {
            return {Issue::ResourceSizeMismatch, signal};
        }
        if (metadata.frameId != frame.frameId)
        {
            return {Issue::FrameMismatch, signal};
        }
        if (metadata.sampleStartIndex != frame.sampleStartIndex || metadata.samplesPerFrame != frame.samplesPerFrame)
        {
            return {Issue::SampleMismatch, signal};
        }
        if (metadata.samplingSignature != frame.samplingSignature)
        {
            return {Issue::SamplingPolicyMismatch, signal};
        }
    }
    return {};
}

std::array<float, 2> PathTracingMotionNdcToUv(const std::array<float, 2>& motion)
{
    return {motion[0] * 0.5f, motion[1] * -0.5f};
}

} // namespace Engine
