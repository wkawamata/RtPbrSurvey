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
    m_pathTracingSampleCommitPending = false;
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    if (!m_ptRrLive || !m_ptRrLive->HoldProduction())
#endif
        m_pathTracingProduction.Invalidate();
    const auto reconstruction = MakePathTracingReconstructionPlan();
    m_frameResources[m_currentFrameIndex].reconstructionPlan = reconstruction;
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    m_rrConversionRecorded = false;
    m_rrConversionHostGate = 6;
#endif
    m_pathTracingGeometryToken = 0;
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
    if (m_sceneHasThinTransmission)
    {
        if (!m_pathTracingReconstructionHistory.ResetRequired(Engine::PathTracingReconstructionBackend::Nrd))
            m_pathTracingReconstructionHistory.RequestReset(Engine::PathTracingReconstructionBackend::Nrd);
        m_nrdComposedOutputValid = false;
        m_nrdLastEvaluationAttempted = false;
        m_nrdLastEvaluationRecorded = false;
        m_nrdLastDispatchCount = 0;
    }
#endif
    m_renderGraphRuntime.Graph().Clear();
    m_renderGraphRuntime.Operations().Clear();

    AddPass(MakeClearPass());

    if (m_sceneResourcesAvailable)
    {
        if (m_renderingPath == RenderingPath::PathTracing)
        {
            bool traceFrame = !m_pathTracingRuntimeState.accumulationPaused && m_pathTracingFrameAdvance;
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
            if (IsPtRrNativeRequested() && m_ptRrLive && m_ptRrLive->HoldProduction())
                traceFrame = false;
            if (IsPtRrNativeRequested() && m_ptRrLive && m_ptRrLive->CanEvaluate())
                AddPass(MakePtRrEvaluatePass());
#endif
            if (m_pathTracingHistoryClearRequired || (!m_pathTracingSettings.accumulate && traceFrame))
            {
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
                m_nrdComposedOutputValid = false;
#endif
                AddPass(MakePathTracingHistoryClearPass());
            }
            if (traceFrame)
            {
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
                m_nrdComposedOutputValid = false;
#endif
                AddPass(MakePathTracingPass());
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
                if (reconstruction.runRrConversion && m_rrConversionPipeline != nullptr)
                {
                    AddPass(MakeRrConversionPass());
                    if (IsPtRrNativeRequested() && m_ptRrLive && m_ptRrLive->CanCapture() &&
                        m_rayReconstructionSupport.IsAvailable())
                        AddPass(MakePtRrValidationPass());
                }
#endif
#ifdef RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS
                if (reconstruction.runNrd && m_nrdConversionPipeline != nullptr && !m_sceneHasThinTransmission)
                {
                    AddPass(MakeNrdConversionPass());
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
                    if (m_nrdReconstructionPipeline != nullptr)
                    {
                        AddPass(MakeNrdReconstructionPass());
                        AddPass(MakeNrdAdmissionPass());
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
                        if (m_nrdGpu != nullptr && m_nrdFailureReason.empty())
                        {
                            AddPass(MakeNrdRelaxPass());
                            AddPass(MakeNrdCompositionPass());
                        }
#endif
                    }
#endif
                }
#endif
            }
        }
        else
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
        }
        AddPass(MakeToneMapPass());
        for (UINT i = 0; i < kMaxDebugTextureOutputCount; ++i)
        {
            if ((m_debugTexturePreviewUpdateSlotMask & (1u << i)) != 0)
            {
                AddPass(MakeDebugTexturePreviewPass(i));
            }
        }

        if (m_renderingPath != RenderingPath::PathTracing && m_debugViewSettings.requestHdrDump)
        {
            AddPass(MakeDebugDumpPass());
        }
    }

    AddPass(MakeImGuiPass());
    if (m_screenshotRequestQueue.CanBeginNextCapture())
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
        if (m_renderingPath == RenderingPath::Deferred && m_rayTracingSupport.IsSupported())
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
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularAlbedo ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionRoughness ||
                    m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularHitDistance)
                {
                    AddPass(MakeReflectionEvaluatePass());
                    if (m_hybridReflectionSettings.contributionEnabled ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedSpecularEstimate ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularVariance ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpecularConfidence ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionSpatialPolicyInputs ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionResolvedRadiance ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionTemporalValidity ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularAlbedo ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionRoughness ||
                        m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularHitDistance)
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
         m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularAlbedo ||
         m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionRoughness ||
         m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularHitDistance ||
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
    if (m_renderingPath == RenderingPath::PathTracing)
    {
        return m_renderGraphRuntime.Authoring()
            .CreatePass(L"Clear")
            .Writes({{kBackBufferResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
            .Rtv(RtvName::BackBuffer)
            .ClearColor(m_backBufferClearColor)
            .Operation(Op::Clear, &RtPbrSurveyEngine::ExecuteClearPass)
            .Build();
    }

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

auto RtPbrSurveyEngine::MakePathTracingHistoryClearPass() -> RenderPass
{
    auto pass =
        m_renderGraphRuntime.Authoring()
            .CreatePass(L"PathTracingHistoryClearPass")
            .Writes({
                {kPathTracingAccumulationResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingSceneColorResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNormalRoughness],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideViewZ], D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideMotionVectors],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideAlbedo], D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDiffuseRadianceHitT],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideSpecularRadianceHitT],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideCurrentFrameRadiance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideMaterialF0Metallic],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideStableRadiance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideBackgroundRadiance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideSecondaryRayData],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuidePhysicalDiffuse],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuidePhysicalSpecular],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuidePrimaryContinuation],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideSecondarySurfaceDistance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserDiffuseDirection],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserSpecularDirection],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserDiffuseDistance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserSpecularDistance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserPrimaryPosition],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#ifdef RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdDiffuseCandidate],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSpecularCandidate],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdConversionDiagnostics],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdNormalRoughness],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdViewZ],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdValidity],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdTiles],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdReconstructedSpecular],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdReconstructedDiffuse],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdInputDiffuse],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdInputSpecular],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdInputViewZ],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdInputAdmission],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdOutputDiffuse],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdOutputSpecular],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdDiffuseFactor],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSpecularFactor],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSceneColor],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdCompositionDiagnostics],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#endif
#endif
#endif
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrColor], D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrDiffuseAlbedo],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrSpecularAlbedo],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrNormal],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrRoughness],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrLinearDepth],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrMotion],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrHitDistance],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrStatus],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrOutput],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrOutputAlternate],
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#endif
            })
            .Operation(Op::PathTracingHistoryClear, &RtPbrSurveyEngine::ExecutePathTracingHistoryClearPass)
            .Build();
    if (!m_pathTracingHistoryClearRequired)
    {
        const auto plan = MakePathTracingReconstructionPlan();
        std::erase_if(pass.writes,
                      [&plan](const Engine::ResourceUsage& usage)
                      {
                          return usage.name.starts_with("PathTracing.RrOutput") ||
                                 (!plan.runNrd && usage.name.starts_with("PathTracing.Nrd")) ||
                                 (!plan.runRrConversion && usage.name.starts_with("PathTracing.Rr"));
                      });
    }
    return pass;
}

auto RtPbrSurveyEngine::MakePathTracingPass() -> RenderPass
{
    Engine::ResourceUsages reads = {
        {kSceneTlasResourceName, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE},
        {kSceneVertexBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneInstanceBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneMeshRangeBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneCameraConstantsResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kMaterialBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kEnvironmentMapResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
    };
    if (m_usesIndexedDraw)
    {
        reads.push_back({kSceneIndexBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ});
    }

    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PathTracingPass")
        .Reads(std::move(reads))
        .Writes({{kPathTracingAccumulationResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingSceneColorResourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNormalRoughness],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideViewZ],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideMotionVectors],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideAlbedo],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDiffuseRadianceHitT],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideSpecularRadianceHitT],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideCurrentFrameRadiance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideMaterialF0Metallic],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideStableRadiance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideBackgroundRadiance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideSecondaryRayData],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuidePhysicalDiffuse],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuidePhysicalSpecular],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuidePrimaryContinuation],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideSecondarySurfaceDistance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserDiffuseDirection],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserSpecularDirection],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserDiffuseDistance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserSpecularDistance],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideDenoiserPrimaryPosition],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::PathTracing, &RtPbrSurveyEngine::ExecutePathTracingPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeRrConversionPass() -> RenderPass
{
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    Engine::ResourceUsages reads;
    Engine::ResourceUsages writes;
    for (Engine::PathTracingInputSignal signal : Engine::kPathTracingRrLiveSignals)
    {
        reads.push_back({kPathTracingGuideTextureResourceNames[GetPathTracingGuideIndex(signal)],
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    }
    for (UINT index = PathTracingGuideRrColor; index <= PathTracingGuideRrStatus; ++index)
    {
        writes.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT RR Input Conversion Diagnostics")
        .Reads(std::move(reads))
        .Writes(std::move(writes))
        .Operation(Op::RrConversion, &RtPbrSurveyEngine::ExecuteRrConversionPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakePtRrValidationPass() -> RenderPass
{
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT RR Input Admission")
        .Reads({{kPathTracingGuideTextureResourceNames[PathTracingGuideRrStatus],
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}})
        .Operation(Op::PtRrValidation, &RtPbrSurveyEngine::ExecutePtRrValidationPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakePtRrEvaluatePass() -> RenderPass
{
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
    Engine::ResourceUsages reads;
    for (UINT index = PathTracingGuideRrColor; index < PathTracingGuideRrStatus; ++index)
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT DLSS RR Native")
        .Reads(std::move(reads))
        .Writes({{kPathTracingGuideTextureResourceNames[PathTracingGuideRrOutput + m_ptRrLive->WritingSlot()],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::PtRrEvaluate, &RtPbrSurveyEngine::ExecutePtRrEvaluatePass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakeNrdConversionPass() -> RenderPass
{
#ifdef RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS
    Engine::ResourceUsages reads = {{kSceneCameraConstantsResourceName, D3D12_RESOURCE_STATE_GENERIC_READ}};
    for (UINT index : {PathTracingGuideNormalRoughness,
                       PathTracingGuideAlbedo,
                       PathTracingGuideMaterialF0Metallic,
                       PathTracingGuidePhysicalDiffuse,
                       PathTracingGuidePhysicalSpecular,
                       PathTracingGuideDenoiserDiffuseDistance,
                       PathTracingGuideDenoiserSpecularDistance,
                       PathTracingGuideDenoiserPrimaryPosition,
                       PathTracingGuideDenoiserDiffuseDirection,
                       PathTracingGuideDenoiserSpecularDirection,
                       PathTracingGuideViewZ})
    {
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT NRD Conversion Diagnostics")
        .Reads(std::move(reads))
        .Writes({{kPathTracingGuideTextureResourceNames[PathTracingGuideNrdDiffuseCandidate],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSpecularCandidate],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdConversionDiagnostics],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdNormalRoughness],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdViewZ],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdValidity],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdDiffuseFactor],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSpecularFactor],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#endif
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdTiles],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
#endif
        })
        .Operation(Op::NrdConversion, &RtPbrSurveyEngine::ExecuteNrdConversionPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakeNrdReconstructionPass() -> RenderPass
{
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
    Engine::ResourceUsages reads = {{kNrdReconstructionConstantsResourceName, D3D12_RESOURCE_STATE_GENERIC_READ}};
    for (UINT index : {PathTracingGuideNrdTiles,
                       PathTracingGuideNrdNormalRoughness,
                       PathTracingGuideNrdViewZ,
                       PathTracingGuideNrdSpecularCandidate,
                       PathTracingGuideNrdDiffuseCandidate})
    {
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT NRD Hit Distance Reconstruction 3x3")
        .Reads(std::move(reads))
        .Writes({{kPathTracingGuideTextureResourceNames[PathTracingGuideNrdReconstructedSpecular],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdReconstructedDiffuse],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::NrdReconstruction, &RtPbrSurveyEngine::ExecuteNrdReconstructionPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakeNrdAdmissionPass() -> RenderPass
{
#ifdef RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS
    Engine::ResourceUsages reads;
    for (UINT index : {PathTracingGuideNrdReconstructedDiffuse,
                       PathTracingGuideNrdReconstructedSpecular,
                       PathTracingGuideDenoiserDiffuseDistance,
                       PathTracingGuideDenoiserSpecularDistance,
                       PathTracingGuideNrdValidity,
                       PathTracingGuideNrdViewZ})
    {
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    }
    Engine::ResourceUsages writes;
    for (UINT index : {PathTracingGuideNrdInputDiffuse,
                       PathTracingGuideNrdInputSpecular,
                       PathTracingGuideNrdInputViewZ,
                       PathTracingGuideNrdInputAdmission})
    {
        writes.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT NRD Reconstruction Input Admission")
        .Reads(std::move(reads))
        .Writes(std::move(writes))
        .Operation(Op::NrdAdmission, &RtPbrSurveyEngine::ExecuteNrdAdmissionPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakeNrdRelaxPass() -> RenderPass
{
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
    Engine::ResourceUsages reads;
    for (UINT index : {PathTracingGuideMotionVectors,
                       PathTracingGuideNrdNormalRoughness,
                       PathTracingGuideNrdInputViewZ,
                       PathTracingGuideNrdInputDiffuse,
                       PathTracingGuideNrdInputSpecular})
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT NRD RELAX GPU Diagnostics")
        .Reads(std::move(reads))
        .Writes({{kPathTracingGuideTextureResourceNames[PathTracingGuideNrdOutputDiffuse],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdOutputSpecular],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::NrdRelax, &RtPbrSurveyEngine::ExecuteNrdRelaxPass)
        .Build();
#else
    return {};
#endif
}

auto RtPbrSurveyEngine::MakeNrdCompositionPass() -> RenderPass
{
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
    Engine::ResourceUsages reads = {
        {kPathTracingSceneColorResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}};
    for (UINT index : {PathTracingGuideNrdOutputDiffuse,
                       PathTracingGuideNrdOutputSpecular,
                       PathTracingGuideNrdDiffuseFactor,
                       PathTracingGuideNrdSpecularFactor,
                       PathTracingGuideStableRadiance,
                       PathTracingGuideBackgroundRadiance,
                       PathTracingGuideNrdInputAdmission})
    {
        reads.push_back({kPathTracingGuideTextureResourceNames[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE});
    }
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"PT NRD Material Restore and Composition")
        .Reads(std::move(reads))
        .Writes({{kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSceneColor],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
                 {kPathTracingGuideTextureResourceNames[PathTracingGuideNrdCompositionDiagnostics],
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS}})
        .Operation(Op::NrdComposition, &RtPbrSurveyEngine::ExecuteNrdCompositionPass)
        .Build();
#else
    return {};
#endif
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
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE},
                {kMaterialBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ}})
        .Writes({{kGBufferResourceNames[Engine::GBuffer::Albedo], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Material], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::MotionVector], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::Emissive], D3D12_RESOURCE_STATE_RENDER_TARGET},
                 {kGBufferResourceNames[Engine::GBuffer::ObjectId], D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::TextureTable, Desc::TextureTable)
        .Descriptor(RootSignatureLayout::InstanceSrv, Desc::InstanceBufferSrv)
        .Descriptor(RootSignatureLayout::MaterialSrv, Desc::MaterialBufferSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Rtvs({RtvName::GBufferAlbedo,
               RtvName::GBufferNormal,
               RtvName::GBufferMaterial,
               RtvName::GBufferMotionVector,
               RtvName::GBufferPBRParams,
               RtvName::GBufferEmissive,
               RtvName::GBufferObjectId})
        .Dsv(DsvName::Depth)
        .Operation(Op::GBuffer, &RtPbrSurveyEngine::ExecuteGBufferPass)
        .Build();
}

auto RtPbrSurveyEngine::MakeHybridReflectionPass() -> RenderPass
{
    Engine::ResourceUsages reads = {
        {kDepthStencilResourceName, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {kGBufferResourceNames[Engine::GBuffer::Normal], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {kGBufferResourceNames[Engine::GBuffer::PBRParams], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {kSceneTlasResourceName, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE},
        {kSceneVertexBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneInstanceBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneMeshRangeBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kSceneCameraConstantsResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
        {kMaterialBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ},
    };
    if (m_usesIndexedDraw)
    {
        reads.push_back({kSceneIndexBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ});
    }

    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"HybridReflectionPass")
        .Reads(std::move(reads))
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
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_WRITE},
                {kMaterialBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ}})
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
    reads.push_back({kMaterialBufferResourceName, D3D12_RESOURCE_STATE_GENERIC_READ});
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
    Engine::ResourceUsages reads;
    if (m_renderingPath == RenderingPath::PathTracing)
    {
        reads.push_back({kPathTracingSceneColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
#ifdef RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS
        if (ResolvePathTracingReconstructionPresentation().output == Engine::PathTracingReconstructionBackend::Rr)
            reads.push_back(
                {kPathTracingGuideTextureResourceNames[PathTracingGuideRrOutput + m_ptRrLive->PublishedSlot()],
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
#endif
#ifdef RTPBRSURVEY_NRD_SDK_DIAGNOSTICS
        if (MakePathTracingReconstructionPlan().backend == Engine::PathTracingReconstructionBackend::Nrd &&
            !m_sceneHasThinTransmission)
        {
            reads.push_back({kPathTracingGuideTextureResourceNames[PathTracingGuideNrdSceneColor],
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
#endif
    }
    else
    {
        reads.push_back({kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        if (ShouldRunTemporalUpscaler())
        {
            reads.push_back({kTemporalUpscalerSceneColorResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
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

auto RtPbrSurveyEngine::MakeDebugTexturePreviewPass(UINT previewIndex) -> RenderPass
{
    const bool bufferSource = m_debugTexturePreviewBufferSources[previewIndex];
    Engine::ResourceUsages reads = {{m_debugTexturePreviewSources[previewIndex],
                                     bufferSource ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                  : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}};
    const char* descriptorName = bufferSource
        ? m_debugTexturePreviewSourceDescriptorNames[previewIndex].c_str()
        : kDebugTexturePreviewDescriptorNames[previewIndex];
    return m_renderGraphRuntime.Authoring()
        .CreatePass(kDebugTexturePreviewPassNames[previewIndex])
        .Pipeline(bufferSource ? Pipe::DebugBufferPreview : Pipe::DebugTexturePreview)
        .Reads(std::move(reads))
        .Writes({{kDebugTexturePreviewResourceNames[previewIndex], D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::ToneMapSceneColor, descriptorName)
        .Rtv(kDebugTexturePreviewRtvNames[previewIndex])
        .Operation(Op::DebugTexturePreview, &RtPbrSurveyEngine::ExecuteDebugTexturePreviewPass)
        .Constants(RootSignatureLayout::ToneMapConstants, kDebugTexturePreviewConstantsNames[previewIndex])
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
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularAlbedo)
    {
        reads.push_back({kReflectionSpecularAlbedoResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionRoughness)
    {
        reads.push_back({kReflectionRoughnessResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularHitDistance)
    {
        reads.push_back({kReflectionSpecularHitDistanceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
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
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularAlbedo)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance, Desc::ReflectionSpecularAlbedoSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionRoughness)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance, Desc::ReflectionRoughnessSrv);
    }
    else if (m_debugViewSettings.renderViewMode == RenderViewMode::RayReconstructionSpecularHitDistance)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionEvaluatedRadiance,
                           Desc::ReflectionSpecularHitDistanceSrv);
    }

    return builder.Build();
}

auto RtPbrSurveyEngine::MakeDebugLinePass() -> RenderPass
{
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"DebugLinePass")
        .Reads({{kDepthStencilResourceName, D3D12_RESOURCE_STATE_DEPTH_READ}})
        .Writes({{kLightPassRenderTargetResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Rtv(RtvName::LightPass)
        .Dsv(DsvName::Depth)
        .Operation(Op::DebugLine, &RtPbrSurveyEngine::ExecuteDebugLinePass)
        .Build();
}

auto RtPbrSurveyEngine::MakeImGuiPass() -> RenderPass
{
    ResourceUsages reads;
    for (UINT i = 0; i < kMaxDebugTextureOutputCount; ++i)
    {
        if ((m_debugTexturePreviewActiveSlotMask & (1u << i)) != 0)
        {
            reads.push_back({kDebugTexturePreviewResourceNames[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
    }

    auto builder = m_renderGraphRuntime.Authoring()
        .CreatePass(L"ImGui")
        .Reads(std::move(reads))
        .Writes({{kBackBufferResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Rtv(RtvName::BackBuffer)
        .Operation(Op::ImGui, &RtPbrSurveyEngine::ExecuteImGuiPass);
    return builder.Build();
}

auto RtPbrSurveyEngine::MakeScreenshotPass() -> RenderPass
{
    const RtPbrSurvey::ScreenshotRequest* request = m_screenshotRequestQueue.PeekNextCapture();
    const char* sourceResourceName = kBackBufferResourceName;
    if (m_screenshotRequestQueue.NextCaptureIsPfm())
    {
        sourceResourceName = kPathTracingAccumulationResourceName;
    }
    else if (request != nullptr && request->source == RtPbrSurvey::ScreenshotCaptureSource::PreToneMapSceneColor)
    {
        sourceResourceName = kLightPassRenderTargetResourceName;
    }

    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"Screenshot")
        .Reads({{sourceResourceName, D3D12_RESOURCE_STATE_COPY_SOURCE}})
        .Operation(Op::Screenshot, &RtPbrSurveyEngine::ExecuteScreenshotPass)
        .Build();
}
