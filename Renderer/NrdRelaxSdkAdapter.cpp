#include "stdafx.h"

#include "NrdRelaxSdkAdapter.h"
#include "Scene/Scene.h"

#include "NRD.h"

#include <algorithm>
#include <cmath>

namespace Engine
{
namespace
{
constexpr nrd::Identifier kRelaxIdentifier = 0;

uint32_t ConvertFormat(nrd::Format format)
{
    switch (format)
    {
        case nrd::Format::RGBA16_SFLOAT:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case nrd::Format::R16_SFLOAT:
            return DXGI_FORMAT_R16_FLOAT;
        case nrd::Format::R8_UNORM:
            return DXGI_FORMAT_R8_UNORM;
        case nrd::Format::RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case nrd::Format::R32_SFLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

NrdSdkResourceKind ConvertResource(nrd::ResourceType type)
{
    switch (type)
    {
        case nrd::ResourceType::IN_MV:
            return NrdSdkResourceKind::Motion;
        case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
            return NrdSdkResourceKind::NormalRoughness;
        case nrd::ResourceType::IN_VIEWZ:
            return NrdSdkResourceKind::ViewZ;
        case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
            return NrdSdkResourceKind::DiffuseInput;
        case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
            return NrdSdkResourceKind::SpecularInput;
        case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
            return NrdSdkResourceKind::DiffuseOutput;
        case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
            return NrdSdkResourceKind::SpecularOutput;
        case nrd::ResourceType::TRANSIENT_POOL:
            return NrdSdkResourceKind::Transient;
        case nrd::ResourceType::PERMANENT_POOL:
            return NrdSdkResourceKind::Permanent;
        default:
            return NrdSdkResourceKind::Unsupported;
    }
}
} // namespace

bool NrdCameraProjectionMatches(const CameraState& previous, const CameraState& current)
{
    return previous.projection == current.projection && previous.fov == current.fov &&
           previous.orthographicHeight == current.orthographicHeight && previous.nearZ == current.nearZ &&
           previous.farZ == current.farZ && previous.lensShiftX == current.lensShiftX &&
           previous.lensShiftY == current.lensShiftY;
}

bool NrdPreviousViewProjectionMatches(const std::array<float, 16>& worldToView,
                                      const std::array<float, 16>& viewToClip,
                                      const std::array<float, 16>& storedPreviousViewProjection)
{
    DirectX::XMFLOAT4X4 view, projection, expected;
    memcpy(&view, worldToView.data(), sizeof(view));
    memcpy(&projection, viewToClip.data(), sizeof(projection));
    DirectX::XMStoreFloat4x4(&expected,
                             DirectX::XMMatrixTranspose(DirectX::XMMatrixMultiply(
                                 DirectX::XMLoadFloat4x4(&view), DirectX::XMLoadFloat4x4(&projection))));
    const float* values = &expected.m[0][0];
    for (size_t i = 0; i < 16; ++i)
    {
        if (!std::isfinite(values[i]) || !std::isfinite(storedPreviousViewProjection[i]) ||
            std::abs(values[i] - storedPreviousViewProjection[i]) > 2e-6f * (std::max)(1.0f, std::abs(values[i])))
        {
            return false;
        }
    }
    return true;
}

struct NrdRelaxSdkAdapter::Impl
{
    nrd::Instance* instance = nullptr;
    ~Impl()
    {
        if (instance != nullptr)
        {
            nrd::DestroyInstance(*instance);
        }
    }
};

NrdRelaxSdkAdapter::NrdRelaxSdkAdapter() = default;
NrdRelaxSdkAdapter::~NrdRelaxSdkAdapter() = default;

bool NrdRelaxSdkAdapter::Initialize(std::string& error, const NrdPrepassSettings& prepass, bool disableAntilag)
{
    Shutdown();
    error.clear();
    if (!std::isfinite(prepass.diffuseRadius) || !std::isfinite(prepass.specularRadius) ||
        prepass.diffuseRadius < 0.0f || prepass.specularRadius < 0.0f)
    {
        error = "NRD prepass radii must be finite and nonnegative.";
        return false;
    }
    const nrd::LibraryDesc* library = nrd::GetLibraryDesc();
    if (library == nullptr || library->versionMajor != 4 || library->versionMinor != 17 || library->versionBuild != 3 ||
        static_cast<uint32_t>(library->normalEncoding) != 4 || static_cast<uint32_t>(library->roughnessEncoding) != 1)
    {
        error = "NRD SDK version or signal encoding mismatch.";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    const nrd::DenoiserDesc denoiser = {kRelaxIdentifier, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR};
    const nrd::InstanceCreationDesc creation = {{}, &denoiser, 1};
    if (nrd::CreateInstance(creation, impl->instance) != nrd::Result::SUCCESS || impl->instance == nullptr)
    {
        error = "NRD CreateInstance failed.";
        return false;
    }
    nrd::RelaxSettings settings;
    settings.diffusePrepassBlurRadius = prepass.diffuseRadius;
    settings.specularPrepassBlurRadius = prepass.specularRadius;
    settings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
    if (disableAntilag)
    {
        settings.antilagSettings.accelerationAmount = 0.0f;
        settings.antilagSettings.resetAmount = 0.0f;
    }
    if (nrd::SetDenoiserSettings(*impl->instance, kRelaxIdentifier, &settings) != nrd::Result::SUCCESS)
    {
        error = "NRD SetDenoiserSettings failed.";
        return false;
    }
    const nrd::InstanceDesc& description = *nrd::GetInstanceDesc(*impl->instance);
    NrdSdkLayout layout;
    if (description.samplersNum != 2 || description.samplers[0] != nrd::Sampler::NEAREST_CLAMP ||
        description.samplers[1] != nrd::Sampler::LINEAR_CLAMP || description.samplersBaseRegisterIndex != 0 ||
        description.resourcesBaseRegisterIndex != 0)
    {
        error = "Unsupported NRD register/sampler layout.";
        return false;
    }
    layout.constantSpace = description.constantBufferAndSamplersSpaceIndex;
    layout.resourceSpace = description.resourcesSpaceIndex;
    layout.constantRegister = description.constantBufferRegisterIndex;
    layout.maximumDispatches = description.descriptorPoolDesc.setsMaxNum;
    for (uint32_t i = 0; i < description.pipelinesNum; ++i)
    {
        const auto& source = description.pipelines[i];
        NrdSdkPipeline pipeline;
        pipeline.identifier = source.shaderIdentifier;
        for (uint32_t j = 0; j < source.resourceRangesNum; ++j)
        {
            const auto& range = source.resourceRanges[j];
            if (range.descriptorType == nrd::DescriptorType::TEXTURE)
                pipeline.textures += range.descriptorsNum;
            else
                pipeline.storageTextures += range.descriptorsNum;
        }
        layout.pipelines.push_back(std::move(pipeline));
    }
    for (uint32_t pool = 0; pool < 2; ++pool)
    {
        const auto* textures = pool == 0 ? description.permanentPool : description.transientPool;
        const uint32_t count = pool == 0 ? description.permanentPoolSize : description.transientPoolSize;
        auto& destination = pool == 0 ? layout.permanent : layout.transient;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t format = ConvertFormat(textures[i].format);
            if (format == DXGI_FORMAT_UNKNOWN || textures[i].downsampleFactor == 0)
            {
                error = "Unsupported NRD pool texture format or dimensions.";
                return false;
            }
            destination.push_back({format, textures[i].downsampleFactor});
        }
    }
    m_description = {"4.17.3",
                     description.pipelinesNum,
                     description.permanentPoolSize,
                     description.transientPoolSize,
                     description.constantBufferMaxDataSize,
                     false,
                     prepass};
    m_description.antilagDisabled = disableAntilag;
    for (uint32_t i = 0; i < description.pipelinesNum; ++i)
    {
        m_description.embeddedDxil |= description.pipelines[i].computeShaderDXIL.bytecode != nullptr;
    }
    m_impl = std::move(impl);
    m_layout = std::move(layout);
    return true;
}

void NrdRelaxSdkAdapter::Shutdown()
{
    m_impl.reset();
    m_description = {};
    m_layout = {};
}

const NrdSdkDescription& NrdRelaxSdkAdapter::Description() const
{
    return m_description;
}

const NrdSdkLayout& NrdRelaxSdkAdapter::Layout() const
{
    return m_layout;
}

bool NrdRelaxSdkAdapter::PrepareFrame(const NrdSdkFrame& frame,
                                      std::vector<NrdSdkDispatch>& dispatches,
                                      std::string& error)
{
    dispatches.clear();
    error.clear();
    const auto validSize = [](uint32_t value) { return value > 0 && value <= UINT16_MAX; };
    const auto validMatrix = [](const std::array<float, 16>& matrix)
    {
        if (!std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); }))
        {
            return false;
        }
        DirectX::XMFLOAT4X4 stored;
        memcpy(&stored, matrix.data(), sizeof(stored));
        const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&stored)));
        return std::isfinite(determinant) && std::abs(determinant) > 1e-12f;
    };
    if (m_impl == nullptr || !validSize(frame.width) || !validSize(frame.height) || !validSize(frame.previousWidth) ||
        !validSize(frame.previousHeight) || !validMatrix(frame.viewToClip) || !validMatrix(frame.previousViewToClip) ||
        !validMatrix(frame.worldToView) || !validMatrix(frame.previousWorldToView))
    {
        error = "NRD SDK is not initialized or frame dimensions/matrices are invalid.";
        return false;
    }
    nrd::CommonSettings common;
    std::copy(frame.viewToClip.begin(), frame.viewToClip.end(), common.viewToClipMatrix);
    std::copy(frame.previousViewToClip.begin(), frame.previousViewToClip.end(), common.viewToClipMatrixPrev);
    std::copy(frame.worldToView.begin(), frame.worldToView.end(), common.worldToViewMatrix);
    std::copy(frame.previousWorldToView.begin(), frame.previousWorldToView.end(), common.worldToViewMatrixPrev);
    common.resourceSize[0] = common.rectSize[0] = static_cast<uint16_t>(frame.width);
    common.resourceSize[1] = common.rectSize[1] = static_cast<uint16_t>(frame.height);
    common.resourceSizePrev[0] = common.rectSizePrev[0] = static_cast<uint16_t>(frame.previousWidth);
    common.resourceSizePrev[1] = common.rectSizePrev[1] = static_cast<uint16_t>(frame.previousHeight);
    std::copy(kNrdScreenMotionVectorScale.begin(), kNrdScreenMotionVectorScale.end(), common.motionVectorScale);
    common.denoisingRange = 1000.0f;
    common.timeDeltaBetweenFrames = 1000.0f / 60.0f;
    common.frameIndex = frame.frameIndex;
    common.accumulationMode = frame.reset ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    if (nrd::SetCommonSettings(*m_impl->instance, common) != nrd::Result::SUCCESS)
    {
        error = "NRD SetCommonSettings failed.";
        return false;
    }
    const nrd::DispatchDesc* sdkDispatches = nullptr;
    uint32_t count = 0;
    if (nrd::GetComputeDispatches(*m_impl->instance, &kRelaxIdentifier, 1, sdkDispatches, count) !=
            nrd::Result::SUCCESS ||
        sdkDispatches == nullptr || count == 0)
    {
        error = "NRD GetComputeDispatches failed.";
        return false;
    }
    const nrd::InstanceDesc& description = *nrd::GetInstanceDesc(*m_impl->instance);
    std::vector<NrdSdkDispatch> owned;
    for (uint32_t i = 0; i < count; ++i)
    {
        const nrd::DispatchDesc& source = sdkDispatches[i];
        if (source.pipelineIndex >= description.pipelinesNum || source.gridWidth == 0 || source.gridHeight == 0 ||
            source.constantBufferDataSize > description.constantBufferMaxDataSize ||
            (source.constantBufferDataSize != 0 && source.constantBufferData == nullptr) ||
            (source.resourcesNum != 0 && source.resources == nullptr))
        {
            error = "NRD returned an invalid dispatch.";
            return false;
        }
        NrdSdkDispatch dispatch;
        dispatch.name = source.name != nullptr ? source.name : "";
        dispatch.pipelineIndex = source.pipelineIndex;
        dispatch.gridWidth = source.gridWidth;
        dispatch.gridHeight = source.gridHeight;
        if (source.constantBufferDataSize != 0)
        {
            dispatch.constants.assign(source.constantBufferData,
                                      source.constantBufferData + source.constantBufferDataSize);
        }
        for (uint32_t j = 0; j < source.resourcesNum; ++j)
        {
            const nrd::ResourceDesc& resource = source.resources[j];
            const NrdSdkResourceKind kind = ConvertResource(resource.type);
            if (kind == NrdSdkResourceKind::Unsupported ||
                (kind == NrdSdkResourceKind::Transient && resource.indexInPool >= description.transientPoolSize) ||
                (kind == NrdSdkResourceKind::Permanent && resource.indexInPool >= description.permanentPoolSize))
            {
                error = "NRD returned an unsupported resource binding.";
                return false;
            }
            dispatch.bindings.push_back(
                {kind, resource.indexInPool, resource.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE});
        }
        owned.push_back(std::move(dispatch));
    }
    dispatches = std::move(owned);
    return true;
}
} // namespace Engine
