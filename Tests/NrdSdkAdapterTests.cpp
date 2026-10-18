#include "Renderer/NrdRelaxSdkAdapter.h"
#include "Renderer/NrdRelaxD3D12.h"
#include <dxgi.h>
#include "Scene/Scene.h"

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
        Require(Engine::IsNrdFatalDeviceResult(DXGI_ERROR_DEVICE_REMOVED) &&
                    Engine::IsNrdFatalDeviceResult(DXGI_ERROR_DEVICE_HUNG) &&
                    Engine::IsNrdFatalDeviceResult(DXGI_ERROR_DEVICE_RESET) &&
                    Engine::IsNrdFatalDeviceResult(DXGI_ERROR_DRIVER_INTERNAL_ERROR),
                "Device errors must never be converted to Native fallback");
        Require(!Engine::IsNrdFatalDeviceResult(S_OK) && !Engine::IsNrdFatalDeviceResult(E_OUTOFMEMORY) &&
                    !Engine::IsNrdFatalDeviceResult(E_INVALIDARG),
                "Non-device HRESULT policy changed");
        Engine::NrdRelaxSdkAdapter adapter;
        std::vector<Engine::NrdSdkDispatch> plan;
        std::string error;
        auto frame = MakeFrame(1920, 1080);
        Engine::CameraState camera, moved;
        moved = camera;
        moved.pos.x += 1.0f;
        moved.gazePoint.y += 1.0f;
        moved.rot.z += 0.1f;
        Require(Engine::NrdCameraProjectionMatches(camera, moved), "Camera pose change rejected");
        for (int changed = 0; changed < 7; ++changed)
        {
            moved = camera;
            switch (changed)
            {
                case 0:
                    moved.fov += 1;
                    break;
                case 1:
                    moved.nearZ += 0.01f;
                    break;
                case 2:
                    moved.farZ += 1;
                    break;
                case 3:
                    moved.orthographicHeight += 1;
                    break;
                case 4:
                    moved.lensShiftX += 0.1f;
                    break;
                case 5:
                    moved.lensShiftY += 0.1f;
                    break;
                case 6:
                    moved.projection = Engine::CameraProjection::Orthographic;
                    break;
            }
            Require(!Engine::NrdCameraProjectionMatches(camera, moved), "Projection change kept NRD history");
        }
        std::array<float, 16> nativePrevious = {};
        DirectX::XMFLOAT4X4 projection, stored;
        memcpy(&projection, frame.viewToClip.data(), sizeof(projection));
        DirectX::XMStoreFloat4x4(&stored, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&projection)));
        memcpy(nativePrevious.data(), &stored, sizeof(stored));
        Require(Engine::NrdPreviousViewProjectionMatches(frame.worldToView, frame.viewToClip, nativePrevious),
                "Matching MV history basis rejected");
        nativePrevious[12] += 0.1f;
        Require(!Engine::NrdPreviousViewProjectionMatches(frame.worldToView, frame.viewToClip, nativePrevious),
                "Skipped evaluation or camera change failed to invalidate MV basis");
        nativePrevious[0] = std::numeric_limits<float>::quiet_NaN();
        Require(!Engine::NrdPreviousViewProjectionMatches(frame.worldToView, frame.viewToClip, nativePrevious),
                "Nonfinite MV history basis accepted");
        Require(!adapter.PrepareFrame(frame, plan, error) && !error.empty() && plan.empty(),
                "Uninitialized SDK accepted frame");
        Require(adapter.Initialize(error), error);
        const auto description = adapter.Description();
        Require(description.prepass.diffuseRadius == 30.0f && description.prepass.specularRadius == 50.0f,
                "Default prepass radii changed");
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
        std::vector<Engine::NrdSdkDispatch> control;
        Require(adapter.PrepareFrame(frame, control, error), error);
        for (const Engine::NrdPrepassSettings settings : {Engine::NrdPrepassSettings{30.0f, 10.0f},
                                                        Engine::NrdPrepassSettings{5.0f, 50.0f}})
        {
            Require(adapter.Initialize(error, settings), error);
            Require(adapter.Description().prepass.diffuseRadius == settings.diffuseRadius &&
                        adapter.Description().prepass.specularRadius == settings.specularRadius,
                    "Prepass description does not match applied settings");
            Require(adapter.PrepareFrame(frame, plan, error), error);
            Require(plan.size() == control.size(), "Prepass variant changed dispatch count");
            bool changed = false;
            for (size_t i = 0; i < plan.size(); ++i)
            {
                Require(plan[i].name == control[i].name && plan[i].pipelineIndex == control[i].pipelineIndex,
                        "Prepass variant changed dispatch structure");
                changed |= plan[i].constants != control[i].constants;
            }
            Require(changed, "Prepass settings did not reach SDK dispatch constants");
        }
        for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()})
        {
            for (bool diffuse : {false, true})
            {
                Engine::NrdPrepassSettings settings;
                (diffuse ? settings.diffuseRadius : settings.specularRadius) = invalid;
                Require(!adapter.Initialize(error, settings) && !error.empty() &&
                            adapter.Description().pipelines == 0,
                        "Invalid prepass settings retained an initialized SDK");
            }
        }
        Require(adapter.Initialize(error, {0.0f, 0.0f}), error);
        Require(adapter.Initialize(error), error);
        Require(adapter.PrepareFrame(frame, plan, error), error);
        Require(plan.size() == control.size(), "Default reinitialization changed dispatch count");
        for (size_t i = 0; i < plan.size(); ++i)
        {
            Require(plan[i].constants == control[i].constants, "Default reinitialization retained variant settings");
        }
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
