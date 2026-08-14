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

#include "Runtime/SceneRenderer.h"

namespace RtPbrSurvey
{
    struct EnvironmentMappingUiState
    {
        Engine::ProceduralEnvironmentSettings settings;
        RtPbrSurveyEngine::LightingParams lighting;
        bool iblEnabled = true;
        bool autoUpdate = Engine::kUseGpuProceduralEnvMap;
        bool reloadPending = false;
    };

    class SceneRendererDebugUi
    {
    public:
        static void Draw(SceneRenderer& renderer,
                         bool* open = nullptr,
                         const char* windowName = "RtPbrSurvey Debug",
                         EnvironmentMappingUiState* environment = nullptr);
        static void DrawEnvironmentMapping(SceneRenderer& renderer, EnvironmentMappingUiState& state);
        static void DrawRenderGraphDiagnostics(SceneRenderer& renderer);
    };
}
