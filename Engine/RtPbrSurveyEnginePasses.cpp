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
    m_renderGraphRuntime.Graph().Clear();
    m_renderGraphRuntime.Operations().Clear();

    AddPass(MakeClearPass());

    if (m_sceneResourcesAvailable)
    {
        AddPass(MakeDepthPrePass());
        AddSceneRenderPasses();
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
                    m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadiance)
                {
                    AddPass(MakeReflectionEvaluatePass());
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
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadiance ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadianceDirect ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadianceIblDiffuse ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadianceIblSpecular ||
         m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadianceEmissive ||
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
            reads.push_back({kReflectionRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        }
        if (m_hybridReflectionSettings.enabled && m_hybridReflectionSettings.hitOverlayEnabled)
        {
            reads.push_back({kReflectionRayHitResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
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
        builder.Descriptor(RootSignatureLayout::ReflectionRadiance, Desc::ReflectionRadianceSrv);
    }
    if (m_rayTracingSupport.IsSupported() && m_hybridReflectionSettings.enabled &&
        m_hybridReflectionSettings.hitOverlayEnabled)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv)
            .Descriptor(RootSignatureLayout::ReflectionRayColor, Desc::ReflectionRayColorSrv);
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
        .Writes({{kReflectionRadianceResourceName, D3D12_RESOURCE_STATE_RENDER_TARGET}})
        .Descriptor(RootSignatureLayout::GBufferSrvBase, Desc::GBufferAlbedoSrv)
        .Descriptor(RootSignatureLayout::EnvironmentMap, Desc::EnvironmentMapSrv)
        .Descriptor(RootSignatureLayout::CameraConstants, Desc::CameraCbv)
        .Descriptor(RootSignatureLayout::ReflectionRayHit, Desc::ReflectionRayHitSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayColor, Desc::ReflectionRayColorSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayMaterial, Desc::ReflectionRayMaterialSrv)
        .Descriptor(RootSignatureLayout::ReflectionRayEmission, Desc::ReflectionRayEmissionSrv)
        .Descriptor(RootSignatureLayout::LightConstants, Desc::LightCbv)
        .Rtv(RtvName::ReflectionRadiance)
        .Operation(Op::ReflectionEvaluate, &RtPbrSurveyEngine::ExecuteReflectionEvaluatePass)
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
    return m_renderGraphRuntime.Authoring()
        .CreatePass(L"ToneMapPass")
        .Pipeline(Pipe::ToneMap)
        .Reads({{GetToneMapSceneColorResourceName(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}})
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
    if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadiance)
    {
        reads.push_back({kReflectionRadianceResourceName, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
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
    if (m_debugViewSettings.renderViewMode == RenderViewMode::ReflectionRadiance)
    {
        builder.Descriptor(RootSignatureLayout::ReflectionRadiance, Desc::ReflectionRadianceSrv);
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
