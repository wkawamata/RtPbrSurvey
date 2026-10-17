#include "Renderer/NrdRelaxSdkAdapter.h"

#include <DirectXMath.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Engine::NrdSdkFrame MakeFrame(uint32_t width, uint32_t height)
{
    Engine::NrdSdkFrame frame;
    frame.width = frame.previousWidth = width;
    frame.height = frame.previousHeight = height;
    DirectX::XMFLOAT4X4 projection, view;
    DirectX::XMStoreFloat4x4(&projection,
                             DirectX::XMMatrixPerspectiveFovLH(1.0f, float(width) / height, 0.1f, 1000.0f));
    DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixIdentity());
    std::copy(&projection.m[0][0], &projection.m[0][0] + 16, frame.viewToClip.begin());
    std::copy(&view.m[0][0], &view.m[0][0] + 16, frame.worldToView.begin());
    frame.previousViewToClip = frame.viewToClip;
    frame.previousWorldToView = frame.worldToView;
    return frame;
}
} // namespace

int main()
{
    try
    {
        Engine::NrdRelaxSdkAdapter adapter;
        std::vector<Engine::NrdSdkDispatch> plan;
        std::string error;
        auto frame = MakeFrame(1920, 1080);
        Require(!adapter.PrepareFrame(frame, plan, error) && !error.empty() && plan.empty(),
                "Uninitialized SDK accepted frame");
        Require(adapter.Initialize(error), error);
        const auto description = adapter.Description();
        Require(description.version == "4.17.3" && description.pipelines > 0 && description.permanentTextures > 0 &&
                    description.transientTextures > 0 && description.maximumConstantBytes > 0 &&
                    !description.embeddedDxil,
                "SDK description mismatch");
        Require(adapter.PrepareFrame(frame, plan, error), error);
        const auto& layout = adapter.Layout();
        Require(layout.pipelines.size() == 27 && layout.permanent.size() == 10 && layout.transient.size() == 7 &&
                    layout.maximumDispatches >= plan.size() && layout.constantSpace == 1 && layout.resourceSpace == 0 &&
                    layout.constantRegister == 0,
                "Pinned SDK GPU layout mismatch");
        for (const auto& texture : layout.permanent)
            Require(texture.dxgiFormat != 0 && texture.downsampleFactor > 0, "Invalid permanent pool layout");
        for (const auto& texture : layout.transient)
            Require(texture.dxgiFormat != 0 && texture.downsampleFactor > 0, "Invalid transient pool layout");
        Require(!plan.empty(), "Empty SDK reset dispatch plan");
        bool diffuseOutput = false, specularOutput = false;
        for (const auto& dispatch : plan)
        {
            const auto& pipeline = layout.pipelines.at(dispatch.pipelineIndex);
            Require(!pipeline.identifier.empty() &&
                        dispatch.bindings.size() == pipeline.textures + pipeline.storageTextures,
                    "Dispatch resources disagree with GPU pipeline");
            for (size_t binding = 0; binding < dispatch.bindings.size(); ++binding)
                Require(dispatch.bindings[binding].storage == (binding >= pipeline.textures),
                        "Unexpected descriptor range order");
            Require(dispatch.pipelineIndex < description.pipelines && dispatch.gridWidth > 0 &&
                        dispatch.gridHeight > 0 && !dispatch.bindings.empty(),
                    "Malformed SDK dispatch");
            for (const auto& binding : dispatch.bindings)
            {
                diffuseOutput |= binding.kind == Engine::NrdSdkResourceKind::DiffuseOutput && binding.storage;
                specularOutput |= binding.kind == Engine::NrdSdkResourceKind::SpecularOutput && binding.storage;
            }
        }
        Require(diffuseOutput && specularOutput, "RELAX plan lacks output bindings");
        const auto resetPlan = plan;
        const auto resetConstants = plan.back().constants;
        frame.reset = false;
        frame.frameIndex = 1;
        std::vector<Engine::NrdSdkDispatch> continued;
        Require(adapter.PrepareFrame(frame, continued, error), error);
        Require(plan.back().constants == resetConstants, "SDK invalidated an owned prior plan");
        Require(continued.size() < resetPlan.size(), "Reset did not add pool-clear dispatches");
        auto resized = MakeFrame(1280, 720);
        Require(adapter.PrepareFrame(resized, continued, error), error);
        bool gridChanged = false;
        for (const auto& dispatch : continued)
        {
            for (const auto& previous : resetPlan)
            {
                if (previous.name == dispatch.name && previous.pipelineIndex == dispatch.pipelineIndex)
                {
                    gridChanged |=
                        previous.gridWidth != dispatch.gridWidth || previous.gridHeight != dispatch.gridHeight;
                }
            }
        }
        Require(gridChanged, "Resize did not change SDK dispatch dimensions");
        for (int invalid = 0; invalid < 4; ++invalid)
        {
            auto bad = frame;
            if (invalid == 0)
                bad.width = 0;
            if (invalid == 1)
                bad.height = 65536;
            if (invalid == 2)
                bad.viewToClip[0] = std::numeric_limits<float>::quiet_NaN();
            if (invalid == 3)
                bad.worldToView = {};
            Require(!adapter.PrepareFrame(bad, continued, error) && continued.empty() && !error.empty(),
                    "Invalid frame accepted");
        }
        adapter.Shutdown();
        adapter.Shutdown();
        Require(adapter.Description().pipelines == 0 && plan.back().constants == resetConstants,
                "Shutdown retained SDK state or invalidated owned plan");
        Require(adapter.Initialize(error), error);
        Require(adapter.Initialize(error), error);
        std::cout << "NRD " << description.version << ": pipelines=" << description.pipelines
                  << " permanent=" << description.permanentTextures << " transient=" << description.transientTextures
                  << " resetDispatches=" << resetPlan.size() << "; CPU planning passed, GPU denoising not enabled\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
