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

#include <memory>

namespace RtPbrSurvey
{
class RenderGraphNodeEditorView
{
public:
    RenderGraphNodeEditorView();
    ~RenderGraphNodeEditorView();

    RenderGraphNodeEditorView(const RenderGraphNodeEditorView&) = delete;
    RenderGraphNodeEditorView& operator=(const RenderGraphNodeEditorView&) = delete;

    void Draw(const Engine::RenderGraphDocument& document);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace RtPbrSurvey
