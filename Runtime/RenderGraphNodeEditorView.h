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

#pragma once

#include "Engine/FrameGraph/RenderGraphDocument.h"
#include "Renderer/DebugResourceViewRegistry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace RtPbrSurvey
{
struct RenderGraphGpuTimingSample
{
    int passIndex = -1;
    float durationMs = 0.0f;
};

struct RenderGraphGpuTimingSnapshot
{
    std::vector<RenderGraphGpuTimingSample> samples;
    float totalGpuTimeMs = 0.0f;
};

struct RenderGraphResourceActions
{
    std::function<bool(const Engine::DebugResourceViewDescriptor&, bool)> openPreview;
    std::function<bool(const std::string&)> isPreviewOpen;
    std::function<uint64_t(const std::string&)> thumbnailTextureId;
    std::function<void(const Engine::DebugResourceViewDescriptor&, bool)> requestThumbnail;
    std::function<void(const std::string&)> closePreview;
    std::function<void()> closeAllPreviews;
    size_t activePreviewCount = 0;
    size_t pinnedPreviewCount = 0;
    size_t maxPreviewCount = 0;
};

struct RenderGraphTechnologyMetadata
{
    std::string dlssSrVersionText;
    std::string dlssRayReconstructionVersionText;
};

class RenderGraphNodeEditorView
{
public:
    RenderGraphNodeEditorView();
    ~RenderGraphNodeEditorView();

    RenderGraphNodeEditorView(const RenderGraphNodeEditorView&) = delete;
    RenderGraphNodeEditorView& operator=(const RenderGraphNodeEditorView&) = delete;

    void Draw(const Engine::RenderGraphDocument& document,
              const RenderGraphGpuTimingSnapshot* timing = nullptr,
              const std::vector<Engine::RenderGraphBarrierDiagnostic>* barrierDiagnostics = nullptr,
              const Engine::DebugResourceViewRegistry* resourceViewRegistry = nullptr,
              const RenderGraphResourceActions* resourceActions = nullptr,
              const RenderGraphTechnologyMetadata* technologyMetadata = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace RtPbrSurvey
