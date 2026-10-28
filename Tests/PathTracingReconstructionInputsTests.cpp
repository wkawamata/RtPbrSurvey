#include "Renderer/PathTracingReconstructionInputs.h"

#include <iostream>
#include <limits>

namespace
{
using Signal = Engine::PathTracingInputSignal;
using Issue = Engine::PathTracingInputIssue;

Engine::PathTracingReconstructionInputs MakeInputs()
{
    Engine::PathTracingReconstructionInputs inputs;
    inputs.frame.frameId = 42;
    inputs.frame.renderWidth = 640;
    inputs.frame.renderHeight = 360;
    inputs.frame.outputWidth = 1280;
    inputs.frame.outputHeight = 720;
    inputs.frame.sampleStartIndex = 10;
    inputs.frame.pixelCenterPrimary = true;
    inputs.frame.samplingSignature = 7;
    const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    inputs.frame.worldToView = identity;
    inputs.frame.viewToClip = identity;
    inputs.frame.previousViewProjection = identity;
    for (size_t i = 0; i < inputs.signals.size(); ++i)
    {
        auto& metadata = inputs.signals[i];
        metadata.present = true;
        metadata.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (i == static_cast<size_t>(Signal::ViewZ))
        {
            metadata.format = DXGI_FORMAT_R32_FLOAT;
        }
        else if (i == static_cast<size_t>(Signal::MotionVectors))
        {
            metadata.format = DXGI_FORMAT_R16G16_FLOAT;
        }
        else if (i >= static_cast<size_t>(Signal::PrimaryPosition))
        {
            metadata.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
        metadata.width = 640;
        metadata.height = 360;
        metadata.frameId = 42;
        metadata.sampleStartIndex = 10;
        metadata.samplesPerFrame = 1;
        metadata.samplingSignature = 7;
    }
    return inputs;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    bool passed = true;
    auto presence = MakeInputs();
    presence.signals[static_cast<size_t>(Signal::SpecularDistance)].present = false;
    presence.signals[static_cast<size_t>(Signal::DiffuseDistance)].present = false;
    const Signal requirements[] = {Signal::SpecularDistance, Signal::DiffuseDistance};
    passed &= Check(Engine::FindMissingPathTracingSignal(presence, requirements) == Signal::SpecularDistance,
                    "backend requirement order determines first missing signal");
    passed &= Check(Engine::FindMissingPathTracingSignal(presence, {}) == Signal::Count,
                    "no additional backend signals required");
    const auto stamp = Engine::MakePathTracingFrameStamp(presence.frame);
    passed &= Check(stamp == Engine::PathTracingFrameStamp{42, 7, 10, 640, 360},
                    "numeric results share the producer frame identity");
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(MakeInputs()).Passed(), "valid full inventory");
    passed &= Check(!Engine::ValidatePathTracingReconstructionInputs({}).Passed(), "default inputs fail closed");
    for (size_t i = 0; i < Engine::kPathTracingInputSignalCount; ++i)
    {
        const auto signal = static_cast<Signal>(i);
        const auto expect = [&](const Engine::PathTracingReconstructionInputs& inputs, Issue issue)
        {
            const auto result = Engine::ValidatePathTracingReconstructionInputs(inputs);
            return result.issue == issue && result.signal == signal;
        };
        auto inputs = MakeInputs();
        inputs.signals[i].frameId = 41;
        passed &= Check(expect(inputs, Issue::FrameMismatch), "stale guide reports its semantic");
        inputs = MakeInputs();
        inputs.signals[i].width = inputs.frame.outputWidth;
        passed &= Check(expect(inputs, Issue::ResourceSizeMismatch), "output-sized input rejected");
        inputs = MakeInputs();
        ++inputs.signals[i].sampleStartIndex;
        passed &= Check(expect(inputs, Issue::SampleMismatch), "different sample range rejected");
        inputs = MakeInputs();
        inputs.signals[i].samplesPerFrame = 4;
        passed &= Check(expect(inputs, Issue::SampleMismatch), "different batch rejected");
        inputs = MakeInputs();
        ++inputs.signals[i].samplingSignature;
        passed &= Check(expect(inputs, Issue::SamplingPolicyMismatch), "different sampling policy rejected");
        inputs = MakeInputs();
        inputs.signals[i].format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        passed &= Check(expect(inputs, Issue::InvalidFormat), "sRGB signal rejected");
        inputs = MakeInputs();
        inputs.signals[i].present = false;
        const auto missing = Engine::ValidatePathTracingReconstructionInputs(inputs);
        passed &= Check(i <= static_cast<size_t>(Signal::MaterialF0Metallic)
                            ? missing.issue == Issue::MissingRequiredSignal && missing.signal == signal
                            : missing.Passed(),
                        "common required inputs and optional backend capabilities are distinct");
    }

    auto inputs = MakeInputs();
    for (size_t i = static_cast<size_t>(Signal::PhysicalDiffuse); i < inputs.signals.size(); ++i)
    {
        inputs.signals[i] = {};
    }
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).Passed(),
                    "common metadata pass does not imply NRD or RR readiness");
    inputs = MakeInputs();
    inputs.frame.samplesPerFrame = 4;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedSampling,
                    "mixed-sample boundary profile deferred");
    inputs = MakeInputs();
    inputs.frame.coherentPrimaryBatch = true;
    inputs.frame.samplesPerFrame = 4;
    for (auto& signal : inputs.signals)
        signal.samplesPerFrame = 4;
    passed &=
        Check(Engine::ValidatePathTracingReconstructionInputs(inputs).Passed(), "coherent primary batch accepted");
    inputs.frame.sampleStartIndex = std::numeric_limits<uint32_t>::max() - 2;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedSampling,
                    "batch sample range overflow rejected");
    inputs = MakeInputs();
    inputs.frame.frameJitterPrimary = true;
    inputs.frame.jitterPixels = {.25f, -.25f};
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).Passed(), "explicit frame jitter accepted");
    inputs.frame.jitterPixels[0] = .51f;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedSampling,
                    "out-of-pixel jitter rejected");
    inputs = MakeInputs();
    inputs.frame.pixelCenterPrimary = false;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedSampling,
                    "stochastic guide association deferred");
    inputs = MakeInputs();
    inputs.frame.jitterPixels[0] = std::numeric_limits<float>::quiet_NaN();
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedSampling,
                    "nonfinite jitter rejected");
    inputs = MakeInputs();
    inputs.frame.preExposure = std::numeric_limits<float>::infinity();
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedExposure,
                    "nonfinite exposure rejected");
    inputs = MakeInputs();
    inputs.frame.exposureScale = 2;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::UnsupportedExposure,
                    "unqualified exposure profile rejected");
    inputs = MakeInputs();
    inputs.frame.previousViewProjection[5] = std::numeric_limits<float>::quiet_NaN();
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::InvalidCameraMatrix,
                    "nonfinite previous matrix rejected");
    inputs = MakeInputs();
    inputs.frame.worldToView = {};
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::InvalidCameraMatrix,
                    "missing matrix rejected");
    inputs = MakeInputs();
    inputs.frame.viewToClip[5] = 0;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::InvalidCameraMatrix,
                    "nonzero singular matrix rejected");
    inputs = MakeInputs();
    // A row-vector perspective projection has a zero final diagonal entry.
    // Validity cannot be decided by diagonal entries or affine-only assumptions.
    inputs.frame.viewToClip = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1.01f, 1, 0, 0, -0.101f, 0};
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).Passed(), "perspective matrix accepted");
    inputs = MakeInputs();
    inputs.frame.renderHeight = 0;
    passed &= Check(Engine::ValidatePathTracingReconstructionInputs(inputs).issue == Issue::InvalidDimensions,
                    "empty render rect rejected");

    // Independent screen-coordinate example: current NDC (0,0), previous
    // (0.25,-0.5) map to current UV (.5,.5), previous UV (.625,.75).
    const auto motion = Engine::PathTracingMotionNdcToUv({0.25f, -0.5f});
    passed &= Check(motion == std::array<float, 2>{0.125f, 0.25f}, "previous-minus-current top-down UV motion");
    passed &= Check(Engine::PathTracingMotionNdcToUv({-0.25f, 0.5f}) == std::array<float, 2>{-0.125f, -0.25f},
                    "reverse motion retains sign");
    if (passed)
    {
        std::cout << "PT reconstruction common metadata contract passed\n";
    }
    return passed ? 0 : 1;
}
