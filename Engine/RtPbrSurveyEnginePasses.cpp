//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"

#include "RtPbrSurveyEngine.h"

void RtPbrSurveyEngine::BuildRenderPasses()
{
    m_temporalUpscalerOutputAvailable = false;
    m_reflectionHistoryCommitPending = false;
    m_reflectionSamplingCommitPending = false;
    m_renderGraphRuntime.Graph().Clear();
    m_renderGraphRuntime.Operations().Clear();

    AddPass(MakeClearPass());

    if (m_sceneResourcesAvailable)
    {
        AddPass(MakeDepthPrePass());
        AddSceneRenderPasses();
        if (m_reflectionHdrDiagnosticRequested)
        {
            AddPass(MakeReflectionHdrDiagnosticPass());
        }
        AddPass(MakeDebugLinePass());
        if (ShouldRunTemporalUpscaler())
        {
            AddPass(MakeTemporalUpscalerPass());
        }
        AddPass(MakeToneMapPass());

        if (m_debugViewSettings.requestHdrDump)
        {
            AddPass(MakeDebugDumpPass());
        }
    }

    AddPass(MakeImGuiPass());
    if (!m_screenshotRequests.empty() && !m_pendingScreenshotCapture.has_value())
    {
        AddPass(MakeScreenshotPass());
    }
}

void RtPbrSurveyEngine::AddSceneRenderPasses()
{
    if (m_renderingPath == RenderingPath::Forward)
    {
        AddPass(MakeForwardPass());
    }
    else
    {
        AddPass(MakeGBufferPass());
        if (m_rayTracingSupport.IsSupported())
        {
            AddPass(MakeRayQueryShadowPass());
            if (m_hybridReflectionSettings.enabled)
            {
                AddPass(MakeHybridReflectionPass());
                if (m_hybridReflectionSettings.contributionEnabled ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadiance ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularEstimate ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity)
                {
                    AddPass(MakeReflectionEvaluatePass());
                    if (m_hybridReflectionSettings.contributionEnabled ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity)
                    {
                        if (ShouldRunRayReconstruction())
                        {
                            AddPass(MakeRayReconstructionRoughnessPass());
                            AddPass(MakeRayReconstructionSpecularAlbedoPass());
                            AddPass(MakeRayReconstructionSpecularHitDistancePass());
                            AddPass(MakeDlssRayReconstructionPass());
                        }
                        else
                        {
                            AddPass(MakeTemporalReflectionPass());
                        }
                        if (m_hybridReflectionSettings.surfaceVarianceFilterEnabled &&
                            m_hybridReflectionSettings.contributionEnabled)
                        {
                            AddPass(MakeEdgeAwareSpatialReflectionPass());
                        }
                    }
                }
            }
            if (m_specularDebugRayQueryRequested)
            {
                AddPass(MakeSpecularDebugRayQueryPass());
            }
            if (m_debugViewSettings.renderViewMode == RenderViewMode::TlasDebug)
            {
                AddPass(MakeRayQueryTlasDebugPass());
            }
        }
        if (m_pixelPickRequested)
        {
            AddPass(MakePixelPickPass());
        }
        AddDeferredSceneOutputPass();
    }
}

void RtPbrSurveyEngine::AddDeferredSceneOutputPass()
{
    if (m_hybridReflectionSettings.enabled &&
        (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayHit ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayDistance ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayNormal ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayColor ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayMaterial ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayEmission ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadiance ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularEstimate ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadianceDirect ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadianceIblDiffuse ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadianceIblSpecular ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadianceEmissive ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRayDistanceFade ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionContributionStrength))
    {
        AddPass(MakeReflectionRayHitDebugPass());
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ShadowMask ||
        m_debugViewSettings.renderViewMode == RenderViewMode::TlasDebug)
    {
        AddPass(MakeShadowMaskDebugPass());
    }
    else if (m_debugViewSettings.IsGBufferDebugView())
    {
        AddPass(MakeGBufferDebugPass());
    }
    else if (m_lightingPassDebugGradientEnabled)
    {
        AddPass(MakeLightingDebugGradientPass());
    }
    else
    {
        AddPass(MakeLightingPass());
    }
}

void RtPbrSurveyEngine::AddPass(RenderPass pass)
{
    m_renderGraphRuntime.Graph().Add(std::move(pass));
}

void RtPbrSurveyEngine::ValidateRenderPassGraph() const
{
    Engine::ValidateRenderPassGraph(
        m_renderGraphRuntime.Graph().Passes(),
        Engine::RenderPassGraphValidationContext<PassOperationHandler>{&m_renderGraphRuntime.Pipelines(),
                                                                       &m_renderGraphRuntime.Bindings(),
                                                                       &m_renderGraphRuntime.Operations(),
                                                                       &m_renderGraphRuntime.Constants()});
}

auto RtPbrSurveyEngine::MakeResourceUsages(std::initializer_list<ResourceUsage> usages) const -> ResourceUsages
{
    return ResourceUsages(usages);
}

auto RtPbrSurveyEngine::MakeGBufferReadUsages() const -> ResourceUsages
{
    return MakeResourceUsages(
        {{kGBufferResourceNames[Engine::GBuffer::Albedo], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kGBufferResourceNames[Engine::GBuffer::Material], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kGBufferResourceNames[Engine::GBuffer::Emissive], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}});
}

PipelineKey RtPbrSurveyEngine::PipelineId(const std::string& name)
{
    return m_renderGraphRuntime.RegisterPipeline(name);
}

DescriptorKey RtPbrSurveyEngine::DescriptorId(const std::string& name)
{
    return m_renderGraphRuntime.RegisterDescriptor(name);
}

auto RtPbrSurveyEngine::MakeClearPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"Clear")
        .Writes({{kBackBufferResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE}})
        .Rtv(RtvName::BackBuffer)
        .Dsv(DsvName::Depth)
        .ClearColor(m_backBufferClearColor)
        .Operation(Op::Clear, &RtPbrSurveyEngine::ExecuteClearPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeDepthPrePass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"Depth PrePass")
        .Pipeline(Pipe::DepthPrePass)
        .Writes({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE}})
        .Descriptor(RootSignatureLayout::InstanceSrv, Desc::InstanceBufferSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Dsv(DsvName::Depth)
        .Operation(Op::DepthPrePass, &RtPbrSurveyEngine::ExecuteDepthPrePass)
        .Build();
}

auto RtPbrSurveyEngine::MakeGBufferPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"GBufferPass")
        .Pipeline(Pipe::GBuffer)
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE}})
        .Writes({{kGBufferResourceNames[Engine::GBuffer::Albedo], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Material], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Emissive], D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::TextureTable, Desc::TextureTable)
        .Descriptor(RootSignatureLayout::InstanceSrv, Desc::InstanceBufferSrv)
        .Descriptor(RootSignatureLayout::MaterialSrv, Desc::MaterialBufferSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Rtvs({RtvName::GBufferAlbedo,
               RtvName::GBufferNormal,
               RtvName::GBufferMaterial,
               RtvName::GBufferMotionVector,
               RtvName::GBufferPBRParams,
               RtvName::GBufferEmissive})
        .Dsv(DsvName::Depth)
        .Operation(Op::GBuffer, &RtPbrSurveyEngine::ExecuteGBufferPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeHybridReflectionPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"HybridReflectionPass")
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kReflectionRayColorResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kReflectionRayMaterialResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kReflectionRayEmissionResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::HybridReflection, &RtPbrSurveyEngine::ExecuteHybridReflectionPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeForwardPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ForwardPass")
        .Pipeline(Pipe::Forward)
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE}})
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::TextureTable, Desc::TextureTable)
        .Descriptor(RootSignatureLayout::InstanceSrv, Desc::InstanceBufferSrv)
        .Descriptor(RootSignatureLayout::MaterialSrv, Desc::MaterialBufferSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Rtv(RtvName::LightPass)
        .Dsv(DsvName::Depth)
        .ClearColor({0.0f, 0.0f, 0.0f, 1.0f})
        .Operation(Op::Forward, &RtPbrSurveyEngine::ExecuteForwardPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRayQueryShadowPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"RayQueryShadowPass")
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}})
        .Writes({{kShadowMaskResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::RayQueryShadow, &RtPbrSurveyEngine::ExecuteRayQueryShadowPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeSpecularDebugRayQueryPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"SpecularDebugRayQueryPass")
        .Operation(Op::SpecularDebugRayQuery, &RtPbrSurveyEngine::ExecuteSpecularDebugRayQueryPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRayQueryTlasDebugPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"RayQueryTlasDebugPass")
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}})
        .Writes({{kShadowMaskResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::RayQueryTlasDebug, &RtPbrSurveyEngine::ExecuteRayQueryTlasDebugPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeLightingPass() -> RenderPass
{
    ResourceUsages reads = MakeGBufferReadUsages();
    if (m_rayTracingSupport.IsSupported())
    {
        reads.push_back({kShadowMaskResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        if (m_hybridReflectionSettings.enabled && m_hybridReflectionSettings.contributionEnabled)
        {
            if (m_hybridReflectionSettings.surfaceVarianceFilterEnabled)
            {
                reads.push_back(
                    {kReflectionDenoisedRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
            }
            else
            {
                const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
                reads.push_back({kReflectionResolvedRadianceResourceNames[writeIndex],
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
            }
        }
        if (m_hybridReflectionSettings.enabled &&
            (m_hybridReflectionSettings.contributionEnabled || m_hybridReflectionSettings.hitOverlayEnabled))
        {
            reads.push_back({kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
        if (m_hybridReflectionSettings.enabled && m_hybridReflectionSettings.hitOverlayEnabled)
        {
            reads.push_back({kReflectionRayColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
    }

    auto builder = m_renderGraphRuntime.Authoring()
        .CreatePass(L"LightPass")
        .Pipeline(Pipe::Lighting)
        .Reads(std::move(reads))
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::MaterialSrv, Desc::MaterialBufferSrv)
        .Descriptor(RootSignatureLayout::EnvironmentMap, Desc::EnvironmentMapSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Descriptor(RootSignatureLayout::ToneMapSceneColor, Desc::ShadowMaskSrv)
        .Rtv(RtvName::LightPass)
        .Operation(Op::Lighting, &RtPbrSurveyEngine::ExecuteLightingPass);

    if (m_rayTracingSupport.IsSupported() && m_hybridReflectionSettings.enabled &&
        m_hybridReflectionSettings.contributionEnabled)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           m_hybridReflectionSettings.surfaceVarianceFilterEnabled ?
                               Desc::ReflectionDenoisedRadianceSrv :
                               Desc::ReflectionResolvedRadianceCurrentSrv);
    }
    if (m_rayTracingSupport.IsSupported() && m_hybridReflectionSettings.enabled &&
        (m_hybridReflectionSettings.contributionEnabled || m_hybridReflectionSettings.hitOverlayEnabled))
    {
        builder.Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv);
    }
    if (m_rayTracingSupport.IsSupported() && m_hybridReflectionSettings.enabled &&
        m_hybridReflectionSettings.hitOverlayEnabled)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionRayColor, Desc::ReflectionRayColorSrv);
    }

    return builder.Build();
}

auto RtPbrSurveyEngine::MakeReflectionEvaluatePass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ReflectionEvaluatePass")
        .Pipeline(Pipe::ReflectionEvaluate)
        .Reads({{kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionRayColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionRayMaterialResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionRayEmissionResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionEvaluatedRadianceResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionSpecularEstimateResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::EnvironmentMap, Desc::EnvironmentMapSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayColor, Desc::ReflectionRayColorSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayMaterial, Desc::ReflectionRayMaterialSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayEmission, Desc::ReflectionRayEmissionSrv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Constants(RootSignatureLayout::ReflectionSamplingConstants, ConstName::ReflectionSampling)
        .Rtvs({RtvName::ReflectionEvaluatedRadiance, RtvName::ReflectionSpecularEstimate})
        .Operation(Op::ReflectionEvaluate, &RtPbrSurveyEngine::ExecuteReflectionEvaluatePass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRayReconstructionRoughnessPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"RayReconstructionRoughnessPass")
        .Pipeline(Pipe::RayReconstructionRoughness)
        .Reads({{kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionRoughnessResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Rtv(RtvName::ReflectionRoughness)
        .Operation(Op::RayReconstructionRoughness, &RtPbrSurveyEngine::ExecuteRayReconstructionRoughnessPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRayReconstructionSpecularAlbedoPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"RayReconstructionSpecularAlbedoPass")
        .Pipeline(Pipe::RayReconstructionSpecularAlbedo)
        .Reads({{kGBufferResourceNames[Engine::GBuffer::Albedo], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionSpecularAlbedoResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Rtv(RtvName::ReflectionSpecularAlbedo)
        .Operation(Op::RayReconstructionSpecularAlbedo,
                   &RtPbrSurveyEngine::ExecuteRayReconstructionSpecularAlbedoPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRayReconstructionSpecularHitDistancePass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"RayReconstructionSpecularHitDistancePass")
        .Pipeline(Pipe::RayReconstructionSpecularHitDistance)
        .Reads({{kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionSpecularHitDistanceResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv)
        .Rtv(RtvName::ReflectionSpecularHitDistance)
        .Operation(Op::RayReconstructionSpecularHitDistance,
                   &RtPbrSurveyEngine::ExecuteRayReconstructionSpecularHitDistancePass)
        .Build();
}

auto RtPbrSurveyEngine::MakeTemporalReflectionPass() -> RenderPass
{
    const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
    Engine::ResourceUsages reads = {
        {kReflectionEvaluatedRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {kReflectionSpecularEstimateResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}};
    if (m_reflectionHistoryState.valid)
    {
        reads.push_back({kReflectionResolvedRadianceResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionHistoryDepthResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionHistoryNormalResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionResolvedSpecularEstimateResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionSpecularMomentsResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionSpecularConfidenceResourceNames[m_reflectionHistoryState.readIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }

    auto builder = m_renderGraphRuntime.Authoring()
        .CreatePass(L"TemporalReflectionPass")
        .Pipeline(Pipe::TemporalReflection)
        .Reads(std::move(reads))
        .Writes({{kReflectionResolvedRadianceResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionHistoryDepthResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionHistoryNormalResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionResolvedSpecularEstimateResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionSpecularMomentsResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kReflectionSpecularConfidenceResourceNames[writeIndex], D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance, Desc::ReflectionEvaluatedRadianceSrv)
        .Descriptor(RootSignatureLayout::ReflectionSpecularEstimate, Desc::ReflectionSpecularEstimateSrv)
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Rtvs({RtvName::ReflectionResolvedRadianceCurrent,
               RtvName::ReflectionHistoryDepthCurrent,
               RtvName::ReflectionHistoryNormalCurrent,
               RtvName::ReflectionResolvedSpecularEstimateCurrent,
               RtvName::ReflectionSpecularMomentsCurrent,
               RtvName::ReflectionSpecularConfidenceCurrent})
        .Operation(Op::TemporalReflection, &RtPbrSurveyEngine::ExecuteTemporalReflectionPass)
        .Constants(RootSignatureLayout::TemporalReflectionConstants, ConstName::TemporalReflection);
    if (m_reflectionHistoryState.valid)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionResolvedRadianceHistory,
                           Desc::ReflectionResolvedRadianceHistorySrv);
        builder.Descriptor(RootSignatureLayout::ReflectionHistoryDepth, Desc::ReflectionHistoryDepthSrv);
        builder.Descriptor(RootSignatureLayout::ReflectionHistoryNormal, Desc::ReflectionHistoryNormalSrv);
        builder.Descriptor(RootSignatureLayout::ReflectionResolvedSpecularEstimateHistory,
                           Desc::ReflectionResolvedSpecularEstimateHistorySrv);
        builder.Descriptor(RootSignatureLayout::ReflectionSpecularMomentsHistory,
                           Desc::ReflectionSpecularMomentsHistorySrv);
        builder.Descriptor(RootSignatureLayout::ReflectionSpecularConfidenceHistory,
                           Desc::ReflectionSpecularConfidenceHistorySrv);
    }
    return builder.Build();
}

auto RtPbrSurveyEngine::MakeDlssRayReconstructionPass() -> RenderPass
{
    const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"DlssRayReconstructionPass")
        .Reads({{kReflectionEvaluatedRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionRoughnessResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionSpecularAlbedoResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionSpecularHitDistanceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionResolvedRadianceResourceNames[writeIndex], D3D12_RESOURCE_STATE_COPY_DEST}})
        .Operation(Op::DlssRayReconstruction, &RtPbrSurveyEngine::ExecuteDlssRayReconstructionPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeEdgeAwareSpatialReflectionPass() -> RenderPass
{
    const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"EdgeAwareSpatialReflectionPass")
        .Pipeline(Pipe::EdgeAwareSpatialReflection)
        .Reads({{kReflectionResolvedRadianceResourceNames[writeIndex],
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionSpecularMomentsResourceNames[writeIndex],
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                {kReflectionSpecularConfidenceResourceNames[writeIndex],
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kReflectionDenoisedRadianceResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                    Desc::ReflectionResolvedRadianceCurrentSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv)
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::ReflectionSpecularMomentsHistory,
                    Desc::ReflectionSpecularMomentsCurrentSrv)
        .Descriptor(RootSignatureLayout::ReflectionSpecularConfidenceHistory,
                    Desc::ReflectionSpecularConfidenceCurrentSrv)
        .Rtv(RtvName::ReflectionDenoisedRadiance)
        .Operation(Op::EdgeAwareSpatialReflection,
                   &RtPbrSurveyEngine::ExecuteEdgeAwareSpatialReflectionPass)
        .Constants(RootSignatureLayout::TemporalReflectionConstants, ConstName::TemporalReflection)
        .Build();
}

auto RtPbrSurveyEngine::MakeLightingDebugGradientPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"LightPassDebugGradient")
        .Pipeline(Pipe::LightingDebugGradient)
        .Reads(MakeGBufferReadUsages())
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::MaterialSrv, Desc::MaterialBufferSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Rtv(RtvName::LightPass)
        .Operation(Op::LightingDebugGradient, &RtPbrSurveyEngine::ExecuteLightingDebugGradientPass)
        .Constants(RootSignatureLayout::ToneMapConstants, ConstName::ToneMap)
        .Build();
}

auto RtPbrSurveyEngine::MakeToneMapPass() -> RenderPass
{
    Engine::ResourceUsages reads = {{kLightPassRenderTargetResourceName,
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}};
    if (ShouldRunTemporalUpscaler())
    {
        reads.push_back({kTemporalUpscalerSceneColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }

    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ToneMapPass")
        .Pipeline(Pipe::ToneMap)
        .Reads(std::move(reads))
        .Writes({{kBackBufferResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ToneMapSceneColor, Desc::ToneMapSceneColorSrv)
        .Rtv(RtvName::BackBuffer)
        .Operation(Op::ToneMap, &RtPbrSurveyEngine::ExecuteToneMapPass)
        .Constants(RootSignatureLayout::ToneMapConstants, ConstName::ToneMap)
        .Build();
}

auto RtPbrSurveyEngine::MakeTemporalUpscalerPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"TemporalUpscalerPass")
        .Reads({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                {kDepthStencilResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
                {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}})
        .Writes({{kTemporalUpscalerSceneColorResourceName, D3D12_RESOURCE_STATE_COPY_DEST}})
        .Operation(Op::TemporalUpscaler, &RtPbrSurveyEngine::ExecuteTemporalUpscalerPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeDebugDumpPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"DebugDump")
        .Reads({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                {kBackBufferResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE}})
        .Operation(Op::DebugDump, &RtPbrSurveyEngine::ExecuteDebugDumpPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeReflectionHdrDiagnosticPass() -> RenderPass
{
    const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
    ResourceUsages reads = {{kReflectionEvaluatedRadianceResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionSpecularEstimateResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionResolvedRadianceResourceNames[writeIndex], D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionResolvedSpecularEstimateResourceNames[writeIndex],
                             D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionSpecularMomentsResourceNames[writeIndex], D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionSpecularConfidenceResourceNames[writeIndex], D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                            {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_COPY_SOURCE}};
    if (m_hybridReflectionSettings.surfaceVarianceFilterEnabled)
    {
        reads.push_back({kReflectionDenoisedRadianceResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ReflectionHdrDiagnostic")
        .Reads(std::move(reads))
        .Operation(Op::ReflectionHdrDiagnostic, &RtPbrSurveyEngine::ExecuteReflectionHdrDiagnosticPass)
        .Build();
}

auto RtPbrSurveyEngine::MakePixelPickPass() -> RenderPass
{
    Engine::ResourceUsages reads = {{kDepthStencilResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE},
                                    {kShadowMaskResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE}};
    for (UINT i = 0; i < Engine::GBuffer::kCount; ++i)
    {
        reads.push_back({kGBufferResourceNames[i], D3D12_RESOURCE_STATE_COPY_SOURCE});
    }

    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PixelPick")
        .Reads(std::move(reads))
        .Operation(Op::PixelPick, &RtPbrSurveyEngine::ExecutePixelPickPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeGBufferDebugPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"GBufferDebugPass")
        .Pipeline(Pipe::GBufferDebug)
        .Reads(MakeGBufferReadUsages())
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Rtv(RtvName::LightPass)
        .Operation(Op::GBufferDebug, &RtPbrSurveyEngine::ExecuteGBufferDebugPass)
        .Constants(RootSignatureLayout::GBufferDebugConstants, ConstName::GBufferDebugTarget)
        .Build();
}

auto RtPbrSurveyEngine::MakeShadowMaskDebugPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ShadowMaskDebugPass")
        .Pipeline(Pipe::ShadowMaskDebug)
        .Reads({{kShadowMaskResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ToneMapSceneColor, Desc::ShadowMaskSrv)
        .Rtv(RtvName::LightPass)
        .Operation(Op::ShadowMaskDebug, &RtPbrSurveyEngine::ExecuteShadowMaskDebugPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeReflectionRayHitDebugPass() -> RenderPass
{
    ResourceUsages reads = {{kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kReflectionRayColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kReflectionRayMaterialResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kReflectionRayEmissionResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kGBufferResourceNames[Engine::GBuffer::Normal],
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kGBufferResourceNames[Engine::GBuffer::PBRParams],
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
                            {kDepthStencilResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}};
    if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadiance)
    {
        reads.push_back({kReflectionEvaluatedRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularEstimate)
    {
        reads.push_back({kReflectionSpecularEstimateResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
             m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity)
    {
        const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
        reads.push_back({kReflectionResolvedRadianceResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate)
    {
        const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
        reads.push_back({kReflectionResolvedSpecularEstimateResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance)
    {
        const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
        reads.push_back({kReflectionSpecularMomentsResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence)
    {
        const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
        reads.push_back({kReflectionSpecularConfidenceResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs)
    {
        const UINT writeIndex = m_reflectionHistoryState.readIndex ^ 1u;
        reads.push_back({kReflectionSpecularMomentsResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        reads.push_back({kReflectionSpecularConfidenceResourceNames[writeIndex],
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }

    auto builder = m_renderGraphRuntime.Authoring()
        .CreatePass(L"ReflectionRayHitDebugPass")
        .Pipeline(Pipe::ReflectionRayHitDebug)
        .Reads(std::move(reads))
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ToneMapSceneColor, Desc::ReflectionRayHitSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayColor, Desc::ReflectionRayColorSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayMaterial, Desc::ReflectionRayMaterialSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayEmission, Desc::ReflectionRayEmissionSrv)
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::EnvironmentMap, Desc::EnvironmentMapSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Rtv(RtvName::LightPass)
        .Operation(Op::ReflectionRayHitDebug, &RtPbrSurveyEngine::ExecuteReflectionRayHitDebugPass)
        .Constants(RootSignatureLayout::GBufferDebugConstants, ConstName::ReflectionRayHitDebugTarget);
    if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionEvaluatedRadiance)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance, Desc::ReflectionEvaluatedRadianceSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularEstimate)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance, Desc::ReflectionSpecularEstimateSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
             m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           Desc::ReflectionResolvedRadianceCurrentSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           Desc::ReflectionResolvedSpecularEstimateCurrentSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           Desc::ReflectionSpecularMomentsCurrentSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           Desc::ReflectionSpecularConfidenceCurrentSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionSpecularMomentsHistory,
                           Desc::ReflectionSpecularMomentsCurrentSrv);
        builder.Descriptor(RootSignatureLayout::ReflectionSpecularConfidenceHistory,
                           Desc::ReflectionSpecularConfidenceCurrentSrv);
    }

    return builder.Build();
}

auto RtPbrSurveyEngine::MakeDebugLinePass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"DebugLinePass")
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Rtv(RtvName::LightPass)
        .Operation(Op::DebugLine, &RtPbrSurveyEngine::ExecuteDebugLinePass)
        .Build();
}

auto RtPbrSurveyEngine::MakeImGuiPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ImGui")
        .Writes({{kBackBufferResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Rtv(RtvName::BackBuffer)
        .Operation(Op::ImGui, &RtPbrSurveyEngine::ExecuteImGuiPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeScreenshotPass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"Screenshot")
        .Reads({{kBackBufferResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE}})
        .Operation(Op::Screenshot, &RtPbrSurveyEngine::ExecuteScreenshotPass)
        .Build();
}
