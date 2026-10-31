cmake_minimum_required(VERSION 3.23)

project(RtPbrSurvey LANGUAGES CXX)

option(RTPBRSURVEY_BUILD_APP "Build the standalone RtPbrSurvey executable." OFF)
option(RTPBRSURVEY_BUILD_SHADERS "Build RtPbrSurvey runtime shaders." ON)
option(RTPBRSURVEY_BUILD_TESTS "Build RtPbrSurvey runtime tests." OFF)
option(RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS "Run experimental live PT-to-RR input conversion diagnostics." OFF)
if(RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS AND NOT RTPBRSURVEY_BUILD_SHADERS)
    message(FATAL_ERROR "RR conversion diagnostics require shader builds.")
endif()
option(RTPBRSURVEY_TEST_NRD_PACKING "Test pinned NRD shader helpers without linking NRD runtime." OFF)
option(RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS "Run experimental PT-to-NRD conversion diagnostics in the application." OFF)
if(RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS AND
   (NOT RTPBRSURVEY_TEST_NRD_PACKING OR NOT RTPBRSURVEY_BUILD_TESTS OR NOT RTPBRSURVEY_BUILD_SHADERS))
    message(FATAL_ERROR "NRD conversion diagnostics require shader builds and the pinned NRD packing test configuration.")
endif()
option(RTPBRSURVEY_TEST_NRD_RECONSTRUCTION "Build pinned NRD reconstruction contract and dispatch tests." OFF)
option(RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS "Run experimental live NRD 3x3 hit-distance reconstruction." OFF)
option(RTPBRSURVEY_NRD_SDK_DIAGNOSTICS "Run pinned RELAX SDK dispatches through the diagnostic D3D12 adapter." OFF)
if(RTPBRSURVEY_NRD_SDK_DIAGNOSTICS AND NOT RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS)
    message(FATAL_ERROR "NRD SDK GPU diagnostics require live reconstruction diagnostics.")
endif()
if(RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS AND
   (NOT RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS OR NOT RTPBRSURVEY_TEST_NRD_RECONSTRUCTION))
    message(FATAL_ERROR "Live NRD reconstruction requires conversion diagnostics and pinned reconstruction tests.")
endif()
if(RTPBRSURVEY_TEST_NRD_RECONSTRUCTION AND NOT RTPBRSURVEY_TEST_NRD_PACKING)
    message(FATAL_ERROR "NRD reconstruction contract tests require RTPBRSURVEY_TEST_NRD_PACKING.")
endif()

set(RTPBRSURVEY_D3D12_NUGET_VERSION "1.618.3" CACHE STRING "Microsoft.Direct3D.D3D12 NuGet version.")
set(RTPBRSURVEY_DXC_NUGET_VERSION "1.8.2505.32" CACHE STRING "Microsoft.Direct3D.DXC NuGet version.")
set(RTPBRSURVEY_WINPIX_NUGET_VERSION "1.0.240308001" CACHE STRING "WinPixEventRuntime NuGet version.")

set(RTPBRSURVEY_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(RTPBRSURVEY_PACKAGES_DIR "${RTPBRSURVEY_ROOT}/packages" CACHE PATH "RtPbrSurvey NuGet packages directory.")
set(RTPBRSURVEY_STREAMLINE_SDK_DIR
    "${RTPBRSURVEY_ROOT}/third_party/streamline-sdk-2.12.0"
    CACHE PATH "Path to the local Streamline SDK root.")

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h REQUIRED)

set(RTPBRSURVEY_D3D12_PACKAGE_DIR
    "${RTPBRSURVEY_PACKAGES_DIR}/Microsoft.Direct3D.D3D12.${RTPBRSURVEY_D3D12_NUGET_VERSION}")
set(RTPBRSURVEY_DXC_PACKAGE_DIR
    "${RTPBRSURVEY_PACKAGES_DIR}/Microsoft.Direct3D.DXC.${RTPBRSURVEY_DXC_NUGET_VERSION}")
set(RTPBRSURVEY_WINPIX_PACKAGE_DIR
    "${RTPBRSURVEY_PACKAGES_DIR}/WinPixEventRuntime.${RTPBRSURVEY_WINPIX_NUGET_VERSION}")

set(RTPBRSURVEY_D3D12_NATIVE_DIR "${RTPBRSURVEY_D3D12_PACKAGE_DIR}/build/native")
set(RTPBRSURVEY_DXC_NATIVE_DIR "${RTPBRSURVEY_DXC_PACKAGE_DIR}/build/native")
set(RTPBRSURVEY_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set_property(GLOBAL PROPERTY RTPBRSURVEY_ROOT "${RTPBRSURVEY_ROOT}")
set_property(GLOBAL PROPERTY RTPBRSURVEY_D3D12_NATIVE_DIR "${RTPBRSURVEY_D3D12_NATIVE_DIR}")
set_property(GLOBAL PROPERTY RTPBRSURVEY_DXC_NATIVE_DIR "${RTPBRSURVEY_DXC_NATIVE_DIR}")
set_property(GLOBAL PROPERTY RTPBRSURVEY_WINPIX_PACKAGE_DIR "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}")
set_property(GLOBAL PROPERTY RTPBRSURVEY_SHADER_OUTPUT_DIR "${RTPBRSURVEY_SHADER_OUTPUT_DIR}")
if(TARGET miniz::miniz)
    get_target_property(rtpbrsurvey_miniz_debug_dll miniz::miniz IMPORTED_LOCATION_DEBUG)
    get_target_property(rtpbrsurvey_miniz_release_dll miniz::miniz IMPORTED_LOCATION_RELEASE)
    set_property(GLOBAL PROPERTY RTPBRSURVEY_MINIZ_DEBUG_DLL "${rtpbrsurvey_miniz_debug_dll}")
    set_property(GLOBAL PROPERTY RTPBRSURVEY_MINIZ_RELEASE_DLL "${rtpbrsurvey_miniz_release_dll}")
endif()

if(NOT EXISTS "${RTPBRSURVEY_D3D12_NATIVE_DIR}/include/d3d12.h")
    message(FATAL_ERROR
        "Microsoft.Direct3D.D3D12 NuGet package was not found. "
        "Run Restore-NuGet.ps1 or set RTPBRSURVEY_PACKAGES_DIR.")
endif()

if(NOT EXISTS "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe")
    message(FATAL_ERROR
        "Microsoft.Direct3D.DXC NuGet package was not found. "
        "Run Restore-NuGet.ps1 or set RTPBRSURVEY_PACKAGES_DIR.")
endif()

set(RTPBRSURVEY_RENDERER_SOURCES
    CubeMesh.cpp
    GltfLoader.cpp
    TiniGgltfImpl.cpp
    WorkMeter.cpp
    Camera/DebugCameraController.cpp
    Engine/Rhi/Dx12/GraphicsDevice.cpp
    Engine/FrameGraph/RenderGraphDocument.cpp
    Engine/RtPbrSurveyEngine.cpp
    Engine/RtPbrSurveyEnginePasses.cpp
    Platform/CommandLineOptions.cpp
    Platform/Win32Application.cpp
    Renderer/AccelerationStructureResources.cpp
    Renderer/AnimatedGifEncoder.cpp
    Renderer/Mp4Encoder.cpp
    Renderer/Mp4EncoderWorker.cpp
    Renderer/BrdfLut.cpp
    Renderer/ClearPass.cpp
    Renderer/DebugDumpCapture.cpp
    Renderer/DebugDumpReport.cpp
    Renderer/DebugLinePass.cpp
    Renderer/DebugResourceViewRegistry.cpp
    Renderer/DebugTexturePreviewPass.cpp
    Renderer/EdgeAwareSpatialReflectionPass.cpp
    Renderer/EnvironmentMap.cpp
    Renderer/FullscreenTriangle.cpp
    Renderer/GBuffer.cpp
    Renderer/HdrOutput.cpp
    Renderer/HybridReflectionPass.cpp
    Renderer/LightingPass.cpp
    Renderer/MaterialBuffer.cpp
    Renderer/NrdRelaxAdapter.cpp
    Renderer/NrdReconstructionInputs.cpp
    Renderer/PathTracingPass.cpp
    Renderer/PathTracingReconstructionInputs.cpp
    Renderer/PathTracingReconstructionPolicy.cpp
    Renderer/PathTracingReconstructionResources.cpp
    Renderer/PathTracingRrInputs.cpp
    Renderer/PathTracingRrHistory.cpp
    Renderer/PathTracingRrLive.cpp
    Renderer/PipelineFactory.cpp
    Renderer/RayQueryShadowPass.cpp
    Renderer/RayQueryTlasDebugPass.cpp
    Renderer/RayReconstructionRoughnessPass.cpp
    Renderer/RayReconstructionSpecularAlbedoPass.cpp
    Renderer/RayReconstructionSpecularHitDistancePass.cpp
    Renderer/RayReconstructionSupport.cpp
    Renderer/RayTracingSupport.cpp
    Renderer/ReflectionEvaluatePass.cpp
    Renderer/ReflectionHdrDiagnosticCapture.cpp
    Renderer/ReflectionHdrDiagnosticStatistics.cpp
    Renderer/ReflectionRayHitDebugPass.cpp
    Renderer/RootSignatureFactory.cpp
    Renderer/SceneGeometryPass.cpp
    Renderer/ScreenshotCapture.cpp
    Renderer/ShadowMaskDebugPass.cpp
    Renderer/SpecularDebugRayQueryPass.cpp
    Renderer/StagedDescriptorAllocator_Test.cpp
    Renderer/StreamlineAdapter.cpp
    Renderer/TemporalReflectionPass.cpp
    Renderer/TemporalUpscalerSupport.cpp
    Renderer/TextureMipChain.cpp
    Renderer/ToneMap.cpp
    Runtime/SceneRenderer.cpp
    Runtime/CaptureSession.cpp
    Runtime/CaptureSessionUi.cpp
    Runtime/CaptureRequestGate.cpp
    Runtime/DebugBufferInspector.cpp
    Runtime/DebugTextureInspector.cpp
    Runtime/DebugTextureThumbnailScheduler.cpp
    Runtime/EvaluationState.cpp
    Runtime/DebugLine.cpp
    Runtime/SceneRendererDebugUi.cpp
    Runtime/RenderGraphNodeEditorView.cpp
    Runtime/SceneRendererSettings.cpp
    App/RenderPresetStore.cpp
    Scene/ProceduralSceneBuilder.cpp
    Scene/SceneBuilder.cpp
    Scene/PathTracingTangents.cpp
    third_party/mikktspace/mikktspace.c
    Scene/EmissiveTriangleTable.cpp
    Scene/SceneDocumentJson.cpp
    Scene/SceneDocumentBuilder.cpp
    Scene/SceneDocumentRuntimeScene.cpp
    Scene/SceneGraph.cpp
    Scene/SceneFactory.cpp
    Scene/SampleScene.cpp
    Ui/DebugUiPreferences.cpp
    Ui/ImGuiSystem.cpp
    stdafx.cpp
    third_party/imgui-node-editor/crude_json.cpp
    third_party/imgui-node-editor/imgui_canvas.cpp
    third_party/imgui-node-editor/imgui_node_editor.cpp
    third_party/imgui-node-editor/imgui_node_editor_api.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
)

set_source_files_properties(
    Scene/PathTracingTangents.cpp
    third_party/mikktspace/mikktspace.c
    third_party/imgui-node-editor/crude_json.cpp
    third_party/imgui-node-editor/imgui_canvas.cpp
    third_party/imgui-node-editor/imgui_node_editor.cpp
    third_party/imgui-node-editor/imgui_node_editor_api.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(third_party/mikktspace/mikktspace.c PROPERTIES LANGUAGE CXX)

add_library(RtPbrSurvey.SceneRenderer STATIC ${RTPBRSURVEY_RENDERER_SOURCES})
add_library(RtPbrSurvey::SceneRenderer ALIAS RtPbrSurvey.SceneRenderer)

target_compile_features(RtPbrSurvey.SceneRenderer PUBLIC cxx_std_20)
target_compile_definitions(RtPbrSurvey.SceneRenderer
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        WIN32
        _WINDOWS)

set(RTPBRSURVEY_HAS_STREAMLINE_SDK OFF)
if(EXISTS "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/include/sl.h" AND
   EXISTS "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/lib/x64/sl.interposer.lib")
    set(RTPBRSURVEY_HAS_STREAMLINE_SDK ON)
    target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_HAS_STREAMLINE_SDK)
    target_include_directories(RtPbrSurvey.SceneRenderer PRIVATE "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/include")
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/lib/x64/sl.interposer.lib")
    if(MSVC)
        target_link_options(RtPbrSurvey.SceneRenderer PUBLIC /DELAYLOAD:sl.interposer.dll)
    endif()
endif()

target_include_directories(RtPbrSurvey.SceneRenderer
    PUBLIC
        "${RTPBRSURVEY_ROOT}"
        "${RTPBRSURVEY_D3D12_NATIVE_DIR}"
        "${RTPBRSURVEY_D3D12_NATIVE_DIR}/include"
        "${RTPBRSURVEY_D3D12_NATIVE_DIR}/include/d3dx12"
        "${RTPBRSURVEY_DXC_NATIVE_DIR}/include"
        "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}/Include/WinPixEventRuntime"
        "${TINYGLTF_INCLUDE_DIR}")

target_precompile_headers(RtPbrSurvey.SceneRenderer PRIVATE "${RTPBRSURVEY_ROOT}/stdafx.h")

target_link_libraries(RtPbrSurvey.SceneRenderer
        PUBLIC
        imgui::imgui
        nlohmann_json::nlohmann_json
        unofficial::tinyexr::tinyexr
        d3dcompiler
        dxguid
        delayimp
        windowscodecs
        mfplat
        mfreadwrite
        mfuuid)

if(NOT RTPBRSURVEY_HAS_STREAMLINE_SDK)
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC d3d12 dxgi)
endif()

if(EXISTS "${RTPBRSURVEY_DXC_NATIVE_DIR}/lib/x64/dxcompiler.lib")
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC "${RTPBRSURVEY_DXC_NATIVE_DIR}/lib/x64/dxcompiler.lib")
endif()

if(EXISTS "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}/bin/x64/WinPixEventRuntime.lib")
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}/bin/x64/WinPixEventRuntime.lib")
endif()

if(MSVC AND NOT RTPBRSURVEY_HAS_STREAMLINE_SDK)
    target_compile_options(RtPbrSurvey.SceneRenderer PRIVATE /permissive)
    target_link_options(RtPbrSurvey.SceneRenderer PUBLIC /DELAYLOAD:d3d12.dll)
elseif(MSVC)
    target_compile_options(RtPbrSurvey.SceneRenderer PRIVATE /permissive)
endif()

function(rtpbrsurvey_add_shader output_var shader_file stage entry_point)
    cmake_parse_arguments(SHADER "" "" "INCLUDE_DIRS;DEPENDS;DEFINITIONS" ${ARGN})
    set(shader_include_args)
    foreach(include_dir IN LISTS SHADER_INCLUDE_DIRS)
        list(APPEND shader_include_args "-I${include_dir}")
    endforeach()
    foreach(definition IN LISTS SHADER_DEFINITIONS)
        list(APPEND shader_include_args "-D${definition}")
    endforeach()
    get_filename_component(shader_name "${shader_file}" NAME_WE)
    set(output_file "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/${shader_name}_${entry_point}.cso")
    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RTPBRSURVEY_SHADER_OUTPUT_DIR}"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe"
            -nologo "-T${stage}" "-E${entry_point}" -Zi -Qembed_debug
            ${shader_include_args} "-Fo${output_file}" "${RTPBRSURVEY_ROOT}/${shader_file}"
        DEPENDS "${RTPBRSURVEY_ROOT}/${shader_file}" ${SHADER_UNPARSED_ARGUMENTS} ${SHADER_DEPENDS}
            "${RTPBRSURVEY_ROOT}/Shaders/DirectLights.hlsli"
            "${RTPBRSURVEY_ROOT}/Shaders/Material.hlsli"
            "${RTPBRSURVEY_ROOT}/Shaders/VolumeAbsorptionParameters.hlsli"
            "${RTPBRSURVEY_ROOT}/Shaders/DirectLightEvaluation.hlsli"
            "${RTPBRSURVEY_ROOT}/Shaders/DirectPbrLighting.hlsli"
            "${RTPBRSURVEY_ROOT}/Shaders/ReflectionSampling.hlsli"
        WORKING_DIRECTORY "${RTPBRSURVEY_ROOT}"
        VERBATIM)
    set(${output_var} "${${output_var}};${output_file}" PARENT_SCOPE)
endfunction()

if(RTPBRSURVEY_BUILD_SHADERS)
    set(RTPBRSURVEY_SHADER_OUTPUTS)
    if(RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS)
        target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS=1)
        rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_PathTracingRrValidation.hlsl cs_6_0 CSMain)
        rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_PathTracingRrConversion.hlsl cs_6_0 CSMain
            DEFINITIONS RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS=1
            DEPENDS "${RTPBRSURVEY_ROOT}/Shaders/RayReconstructionInputConversion.hlsli")
    endif()
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DepthOnlyVS.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBuffer.hlsl vs_6_0 VSMain
        "${RTPBRSURVEY_ROOT}/Shaders/SurfaceTransform.hlsli")
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBuffer.hlsl ps_6_0 PSMain
        "${RTPBRSURVEY_ROOT}/Shaders/SurfaceTransform.hlsli")
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBufferDebug.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBufferDebug.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ShadowMaskDebug.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ShadowMaskDebug.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ReflectionRayHitDebug.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ReflectionRayHitDebug.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_LightPass.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_LightPass.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_LightPassDebugGradient.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_LightPassDebugGradient.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ReflectionEvaluate.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ReflectionEvaluate.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_TemporalReflection.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_TemporalReflection.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_EdgeAwareSpatialReflection.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_EdgeAwareSpatialReflection.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionRoughness.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionRoughness.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionSpecularAlbedo.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionSpecularAlbedo.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionSpecularHitDistance.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayReconstructionSpecularHitDistance.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ToneMap.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ToneMap.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugLine.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugLine.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugTexturePreview.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugTexturePreview.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugBufferPreview.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugBufferPreview.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ProceduralEnvMap.hlsl cs_6_0 CSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayQueryShadow.hlsl cs_6_5 CSMain)
    rtpbrsurvey_add_shader(
        RTPBRSURVEY_SHADER_OUTPUTS
        Shaders/shaders_HybridReflection.hlsl
        cs_6_5
        CSMain
        "${RTPBRSURVEY_ROOT}/Shaders/AlphaMaskCoverage.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/SceneRayQuery.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/SurfaceTransform.hlsli")
    rtpbrsurvey_add_shader(
        RTPBRSURVEY_SHADER_OUTPUTS
        Shaders/shaders_PathTracing.hlsl
        cs_6_5
        CSMain
        "${RTPBRSURVEY_ROOT}/Shaders/AlphaMaskCoverage.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/Material.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/PathTracingSampling.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/ThinTransmission.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/VolumeTransmission.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/RoughVolumeTransmission.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/PathTracingDenoiserSampling.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/EmissiveTriangleSampling.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/PathTracingDirectLightAdapter.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/SceneRayQuery.hlsli"
        "${RTPBRSURVEY_ROOT}/Shaders/SurfaceTransform.hlsli")
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayQueryTlasDebug.hlsl cs_6_5 CSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_SpecularDebugRayQuery.hlsl cs_6_5 CSMain)

    add_custom_target(RtPbrSurvey.Shaders DEPENDS ${RTPBRSURVEY_SHADER_OUTPUTS})
    add_dependencies(RtPbrSurvey.SceneRenderer RtPbrSurvey.Shaders)
endif()

function(rtpbrsurvey_copy_runtime_files target)
    get_property(rtpbrsurvey_root GLOBAL PROPERTY RTPBRSURVEY_ROOT)
    get_property(rtpbrsurvey_d3d12_native_dir GLOBAL PROPERTY RTPBRSURVEY_D3D12_NATIVE_DIR)
    get_property(rtpbrsurvey_dxc_native_dir GLOBAL PROPERTY RTPBRSURVEY_DXC_NATIVE_DIR)
    get_property(rtpbrsurvey_winpix_package_dir GLOBAL PROPERTY RTPBRSURVEY_WINPIX_PACKAGE_DIR)
    get_property(rtpbrsurvey_shader_output_dir GLOBAL PROPERTY RTPBRSURVEY_SHADER_OUTPUT_DIR)
    get_property(rtpbrsurvey_miniz_debug_dll GLOBAL PROPERTY RTPBRSURVEY_MINIZ_DEBUG_DLL)
    get_property(rtpbrsurvey_miniz_release_dll GLOBAL PROPERTY RTPBRSURVEY_MINIZ_RELEASE_DLL)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${rtpbrsurvey_root}/Assets"
            "$<TARGET_FILE_DIR:${target}>/Assets"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${rtpbrsurvey_d3d12_native_dir}/bin/x64"
            "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${rtpbrsurvey_dxc_native_dir}/bin/x64/dxcompiler.dll"
            "${rtpbrsurvey_dxc_native_dir}/bin/x64/dxil.dll"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${rtpbrsurvey_winpix_package_dir}/bin/x64/WinPixEventRuntime.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)

    if(rtpbrsurvey_miniz_debug_dll AND rtpbrsurvey_miniz_release_dll)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${rtpbrsurvey_miniz_debug_dll},${rtpbrsurvey_miniz_release_dll}>"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()

    if(RTPBRSURVEY_HAS_STREAMLINE_SDK)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/sl.interposer.dll"
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/sl.common.dll"
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/sl.dlss.dll"
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/sl.dlss_d.dll"
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/nvngx_dlss.dll"
                "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/nvngx_dlssd.dll"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()

    if(TARGET RtPbrSurvey.Shaders)
        add_dependencies(${target} RtPbrSurvey.Shaders)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${rtpbrsurvey_shader_output_dir}"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

if(RTPBRSURVEY_BUILD_APP)
    add_executable(RtPbrSurvey WIN32
        Main.cpp
        App/DebugUi.cpp
        App/EvaluationCaseUi.cpp
        App/RtPbrSurveyApp.cpp
        App/PathTracingHistoryValidation.cpp
        App/SceneConfig.cpp
        App/SceneEditorSession.cpp
        App/SceneEditorUi.cpp
        App/SceneSelectUi.cpp)
    target_link_libraries(RtPbrSurvey PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey PRIVATE /permissive)
    endif()
    rtpbrsurvey_copy_runtime_files(RtPbrSurvey)
endif()

if(RTPBRSURVEY_BUILD_TESTS)
    enable_testing()

    add_test(NAME RtPbrSurvey.RuntimeShaderManifest
        COMMAND "${CMAKE_COMMAND}"
            "-DRTPBRSURVEY_ROOT=${RTPBRSURVEY_ROOT}"
            -P "${RTPBRSURVEY_ROOT}/Tests/VerifyRuntimeShaders.cmake")
    add_executable(RtPbrSurvey.DebugTextureInspectorTests
        Tests/DebugTextureInspectorTests.cpp)
    target_link_libraries(RtPbrSurvey.DebugTextureInspectorTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.DebugTextureInspectorTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.DebugTextureInspector COMMAND RtPbrSurvey.DebugTextureInspectorTests)

    add_executable(RtPbrSurvey.RenderGraphDocumentTests
        Tests/RenderGraphDocumentTests.cpp)
    target_link_libraries(RtPbrSurvey.RenderGraphDocumentTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.RenderGraphDocumentTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.RenderGraphDocument COMMAND RtPbrSurvey.RenderGraphDocumentTests)

    add_executable(RtPbrSurvey.SceneRendererSettingsTests
        Tests/SceneRendererSettingsTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneRendererSettingsTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneRendererSettingsTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneRendererSettings COMMAND RtPbrSurvey.SceneRendererSettingsTests)

    add_executable(RtPbrSurvey.PathTracingShadowSettingsTests
        Tests/PathTracingShadowSettingsTests.cpp)
    target_link_libraries(RtPbrSurvey.PathTracingShadowSettingsTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.PathTracingShadowSettingsTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.PathTracingShadowSettings COMMAND RtPbrSurvey.PathTracingShadowSettingsTests)

    add_executable(RtPbrSurvey.PathTracingHistoryTests
        Tests/PathTracingHistoryTests.cpp)
    target_link_libraries(RtPbrSurvey.PathTracingHistoryTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.PathTracingHistoryTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.PathTracingHistory COMMAND RtPbrSurvey.PathTracingHistoryTests)

    add_executable(RtPbrSurvey.PathTracingReconstructionInputsTests
        Tests/PathTracingReconstructionInputsTests.cpp
        Renderer/PathTracingReconstructionInputs.cpp)
    target_include_directories(RtPbrSurvey.PathTracingReconstructionInputsTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.PathTracingReconstructionInputsTests PRIVATE cxx_std_20)
    add_test(NAME RtPbrSurvey.PathTracingReconstructionInputs COMMAND RtPbrSurvey.PathTracingReconstructionInputsTests)

    add_executable(RtPbrSurvey.PathTracingReconstructionResourcesTests
        Tests/PathTracingReconstructionResourcesTests.cpp
        Renderer/PathTracingReconstructionInputs.cpp
        Renderer/PathTracingReconstructionResources.cpp)
    target_include_directories(RtPbrSurvey.PathTracingReconstructionResourcesTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.PathTracingReconstructionResourcesTests PRIVATE cxx_std_20)
    target_compile_definitions(RtPbrSurvey.PathTracingReconstructionResourcesTests PRIVATE NOMINMAX)
    target_link_libraries(RtPbrSurvey.PathTracingReconstructionResourcesTests PRIVATE d3d12 dxgi)
    add_test(NAME RtPbrSurvey.PathTracingReconstructionResources COMMAND RtPbrSurvey.PathTracingReconstructionResourcesTests)

    add_executable(RtPbrSurvey.NrdReconstructionInputsTests
        Tests/NrdReconstructionInputsTests.cpp
        Renderer/PathTracingReconstructionInputs.cpp
        Renderer/NrdReconstructionInputs.cpp)
    target_include_directories(RtPbrSurvey.NrdReconstructionInputsTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.NrdReconstructionInputsTests PRIVATE cxx_std_20)
    target_compile_definitions(RtPbrSurvey.NrdReconstructionInputsTests PRIVATE
        RTPBRSURVEY_NRD_INPUT_SHADER_PATH="${RTPBRSURVEY_ROOT}/Shaders/shaders_NrdConversion.hlsl")
    add_test(NAME RtPbrSurvey.NrdReconstructionInputs COMMAND RtPbrSurvey.NrdReconstructionInputsTests)

    add_executable(RtPbrSurvey.PathTracingRrInputsTests Tests/PathTracingRrInputsTests.cpp
        Renderer/PathTracingReconstructionInputs.cpp Renderer/PathTracingRrInputs.cpp)
    target_include_directories(RtPbrSurvey.PathTracingRrInputsTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.PathTracingRrInputsTests PRIVATE cxx_std_20)
    target_compile_definitions(RtPbrSurvey.PathTracingRrInputsTests PRIVATE
        RTPBRSURVEY_RR_INPUT_SHADER_PATH="${RTPBRSURVEY_ROOT}/Shaders/shaders_PathTracingRrConversion.hlsl")
    add_test(NAME RtPbrSurvey.PathTracingRrInputs COMMAND RtPbrSurvey.PathTracingRrInputsTests)
    add_executable(RtPbrSurvey.PathTracingReconstructionPolicyTests Tests/PathTracingReconstructionPolicyTests.cpp
        Renderer/PathTracingReconstructionPolicy.cpp)
    target_include_directories(RtPbrSurvey.PathTracingReconstructionPolicyTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.PathTracingReconstructionPolicyTests PRIVATE cxx_std_20)
    add_test(NAME RtPbrSurvey.PathTracingReconstructionPolicy COMMAND RtPbrSurvey.PathTracingReconstructionPolicyTests)
    add_executable(RtPbrSurvey.PathTracingRrHistoryTests Tests/PathTracingRrHistoryTests.cpp
        Renderer/PathTracingRrHistory.cpp Renderer/PathTracingReconstructionInputs.cpp)
    target_include_directories(RtPbrSurvey.PathTracingRrHistoryTests PRIVATE "${RTPBRSURVEY_ROOT}")
    target_compile_features(RtPbrSurvey.PathTracingRrHistoryTests PRIVATE cxx_std_20)
    add_test(NAME RtPbrSurvey.PathTracingRrHistory COMMAND RtPbrSurvey.PathTracingRrHistoryTests)
    add_executable(RtPbrSurvey.RayReconstructionInputTests Tests/RayReconstructionInputTests.cpp)
    target_link_libraries(RtPbrSurvey.RayReconstructionInputTests PRIVATE RtPbrSurvey::SceneRenderer d3d12 dxgi)
    add_test(NAME RtPbrSurvey.RayReconstructionInputs COMMAND RtPbrSurvey.RayReconstructionInputTests)

    add_executable(RtPbrSurvey.NrdRelaxAdapterTests Tests/NrdRelaxAdapterTests.cpp)
    target_link_libraries(RtPbrSurvey.NrdRelaxAdapterTests PRIVATE RtPbrSurvey::SceneRenderer)
    target_compile_definitions(RtPbrSurvey.NrdRelaxAdapterTests PRIVATE
        RTPBRSURVEY_NRD_PIXEL_CONTRACT_VECTORS_PATH="${RTPBRSURVEY_ROOT}/Tests/PathTracing/nrd_pixel_contract_vectors.json")
    add_test(NAME RtPbrSurvey.NrdRelaxAdapter COMMAND RtPbrSurvey.NrdRelaxAdapterTests)

    set(denoiser_sampling_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_DenoiserSampling_CSMain.cso")
    add_custom_command(OUTPUT "${denoiser_sampling_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${denoiser_sampling_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_DenoiserSampling.hlsl"
        DEPENDS Tests/shaders_DenoiserSampling.hlsl Shaders/PathTracingDenoiserSampling.hlsli
        VERBATIM)
    add_executable(RtPbrSurvey.PathTracingDenoiserSamplingTests
        Tests/PathTracingDenoiserSamplingTests.cpp "${denoiser_sampling_shader}")
    target_compile_features(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE cxx_std_17)
    target_link_libraries(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE d3d12 dxgi)
    target_compile_definitions(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE NOMINMAX)
    add_test(NAME RtPbrSurvey.PathTracingDenoiserSampling
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${denoiser_sampling_shader}")
    set(ggx_precision_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_GgxPrecision_CSMain.cso")
    add_custom_command(OUTPUT "${ggx_precision_shader}"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${ggx_precision_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_GgxPrecision.hlsl"
        DEPENDS Tests/shaders_GgxPrecision.hlsl Shaders/PathTracingSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${ggx_precision_shader}")
    add_test(NAME RtPbrSurvey.PathTracingGgxPrecision
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${ggx_precision_shader}" --ggx-precision)
    set(rough_volume_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_RoughVolumeTransmission_CSMain.cso")
    add_custom_command(OUTPUT "${rough_volume_shader}"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${rough_volume_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_RoughVolumeTransmission.hlsl"
        DEPENDS Tests/shaders_RoughVolumeTransmission.hlsl Shaders/RoughVolumeTransmission.hlsli
            Shaders/VolumeTransmission.hlsli Shaders/PathTracingDenoiserSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${rough_volume_shader}")
    add_test(NAME RtPbrSurvey.RoughVolumeTransmission
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${rough_volume_shader}" --rough-volume)
    set(rr_conversion_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_RrInputConversion_CSMain.cso")
    set(rr_texture_conversion_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_PathTracingRrConversion_CSMain.cso")
    set(rr_live_distance_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_RrLiveDistance_CSMain.cso")
    add_custom_command(OUTPUT "${rr_conversion_shader}" "${rr_texture_conversion_shader}" "${rr_live_distance_shader}"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${rr_conversion_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_RrInputConversion.hlsl"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${rr_texture_conversion_shader}" "${RTPBRSURVEY_ROOT}/Shaders/shaders_PathTracingRrConversion.hlsl"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${rr_live_distance_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_RrLiveDistance.hlsl"
        DEPENDS Tests/shaders_RrInputConversion.hlsl Shaders/shaders_PathTracingRrConversion.hlsl
            Shaders/RayReconstructionInputConversion.hlsli Tests/shaders_RrLiveDistance.hlsl
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE
        "${rr_conversion_shader}" "${rr_texture_conversion_shader}" "${rr_live_distance_shader}"
        Tests/RrInputConversionValidation.h)
    add_test(NAME RtPbrSurvey.RrInputConversion
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${rr_conversion_shader}" --rr-input-conversion)
    add_test(NAME RtPbrSurvey.RrLiveDistance
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${rr_live_distance_shader}" --rr-live-distance)
    add_executable(RtPbrSurvey.RrTextureConversionTests Tests/RrTextureConversionTests.cpp
        "${rr_texture_conversion_shader}")
    target_compile_features(RtPbrSurvey.RrTextureConversionTests PRIVATE cxx_std_17)
    target_compile_definitions(RtPbrSurvey.RrTextureConversionTests PRIVATE NOMINMAX)
    target_link_libraries(RtPbrSurvey.RrTextureConversionTests PRIVATE d3d12 dxgi)
    add_executable(RtPbrSurvey.RrNativeReplay Tests/RrTextureConversionTests.cpp Tests/RrNativeReplay.h
        "${rr_texture_conversion_shader}")
    target_compile_definitions(RtPbrSurvey.RrNativeReplay PRIVATE RTPBRSURVEY_RR_NATIVE_REPLAY=1 NOMINMAX)
    target_link_libraries(RtPbrSurvey.RrNativeReplay PRIVATE RtPbrSurvey::SceneRenderer d3d12 dxgi)
    if(RTPBRSURVEY_HAS_STREAMLINE_SDK)
        foreach(rr_runtime IN ITEMS sl.interposer sl.common sl.dlss sl.dlss_d nvngx_dlss nvngx_dlssd)
            add_custom_command(TARGET RtPbrSurvey.RrNativeReplay POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/bin/x64/${rr_runtime}.dll"
                    "$<TARGET_FILE_DIR:RtPbrSurvey.RrNativeReplay>" VERBATIM)
        endforeach()
    endif()
    set(thin_transmission_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_ThinTransmission_CSMain.cso")
    add_custom_command(OUTPUT "${thin_transmission_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${thin_transmission_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_ThinTransmission.hlsl"
        DEPENDS Tests/shaders_ThinTransmission.hlsl Shaders/ThinTransmission.hlsli
            Shaders/PathTracingSampling.hlsli Shaders/PathTracingDenoiserSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${thin_transmission_shader}")
    add_test(NAME RtPbrSurvey.ThinTransmission
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${thin_transmission_shader}" --thin-transmission)
    set(rough_thin_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_RoughThinTransmission_CSMain.cso")
    add_custom_command(OUTPUT "${rough_thin_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${rough_thin_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_RoughThinTransmission.hlsl"
        DEPENDS Tests/shaders_RoughThinTransmission.hlsl Shaders/ThinTransmission.hlsli
            Shaders/PathTracingSampling.hlsli Shaders/PathTracingDenoiserSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${rough_thin_shader}")
    add_test(NAME RtPbrSurvey.RoughThinTransmission
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${rough_thin_shader}" --rough-thin-transmission)
    set(thin_metallic_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_ThinMetallicMixture_CSMain.cso")
    add_custom_command(OUTPUT "${thin_metallic_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${thin_metallic_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_ThinMetallicMixture.hlsl"
        DEPENDS Tests/shaders_ThinMetallicMixture.hlsl Shaders/ThinTransmission.hlsli
            Shaders/PathTracingSampling.hlsli Shaders/PathTracingDenoiserSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${thin_metallic_shader}")
    add_test(NAME RtPbrSurvey.ThinMetallicMixture
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${thin_metallic_shader}" --thin-metallic)
    set(alpha_mask_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_AlphaMask_CSMain.cso")
    add_custom_command(OUTPUT "${alpha_mask_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${alpha_mask_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_AlphaMask.hlsl"
        DEPENDS Tests/shaders_AlphaMask.hlsl Shaders/AlphaMaskCoverage.hlsli Shaders/EmissiveTriangleSampling.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${alpha_mask_shader}")
    add_test(NAME RtPbrSurvey.AlphaMask
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${alpha_mask_shader}" --alpha-mask)
    set(volume_transmission_shader "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_VolumeTransmission_CSMain.cso")
    add_custom_command(OUTPUT "${volume_transmission_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Tests"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
            "-Fo${volume_transmission_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_VolumeTransmission.hlsl"
        DEPENDS Tests/shaders_VolumeTransmission.hlsl Shaders/VolumeTransmission.hlsli Shaders/VolumeAbsorptionParameters.hlsli
        VERBATIM)
    target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${volume_transmission_shader}"
        Tests/VolumeTransmissionReference.h Shared/VolumeAbsorptionParameters.h)
    add_test(NAME RtPbrSurvey.VolumeTransmission
        COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${volume_transmission_shader}" --volume-transmission)
    if(RTPBRSURVEY_TEST_NRD_PACKING)
        find_package(Git REQUIRED)
        set(RTPBRSURVEY_NRD_SDK_DIR "${RTPBRSURVEY_ROOT}/third_party/nrd-sdk-4.17.3"
            CACHE PATH "Pinned NRD source checkout for optional shader contract tests.")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SDK_DIR}" rev-parse HEAD
            OUTPUT_VARIABLE nrd_test_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE nrd_test_git_result)
        if(NOT nrd_test_git_result EQUAL 0 OR NOT nrd_test_commit STREQUAL "792eff196afdd350fd9c3f862119017ccb438a0e")
            message(FATAL_ERROR "NRD shader tests require the exact v4.17.3 source commit.")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SDK_DIR}" diff --quiet HEAD -- Shaders/NRD.hlsli
            RESULT_VARIABLE nrd_test_dirty)
        if(NOT nrd_test_dirty EQUAL 0)
            message(FATAL_ERROR "NRD shader tests require an unmodified NRD.hlsli.")
        endif()
        set(nrd_test_dir "${CMAKE_CURRENT_BINARY_DIR}/Tests/NrdPacking")
        configure_file(Tests/NRDShaderContractConfig.hlsli "${nrd_test_dir}/NRDConfig.hlsli" COPYONLY)
        configure_file("${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/NRD.hlsli" "${nrd_test_dir}/NRD.hlsli" COPYONLY)
        set(nrd_packing_shader "${nrd_test_dir}/shaders_NrdRelaxPacking_CSMain.cso")
        add_custom_command(OUTPUT "${nrd_packing_shader}"
            COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                "-I${nrd_test_dir}"
                "-Fo${nrd_packing_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdRelaxPacking.hlsl"
            DEPENDS Tests/shaders_NrdRelaxPacking.hlsl "${nrd_test_dir}/NRDConfig.hlsli"
                "${nrd_test_dir}/NRD.hlsli"
            VERBATIM)
        target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${nrd_packing_shader}")
        add_test(NAME RtPbrSurvey.NrdRelaxPacking
            COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${nrd_packing_shader}" --packing)
        set(nrd_material_shader "${nrd_test_dir}/shaders_NrdMaterialFactors_CSMain.cso")
        add_custom_command(OUTPUT "${nrd_material_shader}"
            COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                "-I${nrd_test_dir}" "-Fo${nrd_material_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdMaterialFactors.hlsl"
            DEPENDS Tests/shaders_NrdMaterialFactors.hlsl "${nrd_test_dir}/NRDConfig.hlsli" "${nrd_test_dir}/NRD.hlsli"
            VERBATIM)
        target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${nrd_material_shader}")
        add_test(NAME RtPbrSurvey.NrdMaterialFactors
            COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${nrd_material_shader}" --material-factors)
        set(nrd_conversion_shader "${nrd_test_dir}/shaders_NrdInputConversion_CSMain.cso")
        add_custom_command(OUTPUT "${nrd_conversion_shader}"
            COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                "-I${nrd_test_dir}" "-Fo${nrd_conversion_shader}" "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdInputConversion.hlsl"
            DEPENDS Tests/shaders_NrdInputConversion.hlsl Shaders/NrdRelaxInputConversion.hlsli
                "${nrd_test_dir}/NRDConfig.hlsli" "${nrd_test_dir}/NRD.hlsli"
            VERBATIM)
        target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${nrd_conversion_shader}")
        add_test(NAME RtPbrSurvey.NrdInputConversion
            COMMAND RtPbrSurvey.PathTracingDenoiserSamplingTests "${nrd_conversion_shader}" --input-conversion)
        set(nrd_captured_conversion_shader "${nrd_test_dir}/shaders_NrdCapturedConversion_CSMain.cso")
        add_custom_command(OUTPUT "${nrd_captured_conversion_shader}"
            COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                -DNRD_CAPTURED_INPUT=1 "-I${nrd_test_dir}" "-Fo${nrd_captured_conversion_shader}"
                "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdInputConversion.hlsl"
            DEPENDS Tests/shaders_NrdInputConversion.hlsl Shaders/NrdRelaxInputConversion.hlsli
                "${nrd_test_dir}/NRDConfig.hlsli" "${nrd_test_dir}/NRD.hlsli"
            VERBATIM)
        target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${nrd_captured_conversion_shader}")
        set(nrd_pixel_contract_shader "${nrd_test_dir}/shaders_NrdPixelContract_CSMain.cso")
        add_custom_command(OUTPUT "${nrd_pixel_contract_shader}"
            COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                "-I${nrd_test_dir}" "-Fo${nrd_pixel_contract_shader}"
                "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdPixelContract.hlsl"
            DEPENDS Tests/shaders_NrdPixelContract.hlsl Shaders/NrdRelaxInputConversion.hlsli
                "${nrd_test_dir}/NRDConfig.hlsli" "${nrd_test_dir}/NRD.hlsli"
            VERBATIM)
        target_sources(RtPbrSurvey.PathTracingDenoiserSamplingTests PRIVATE "${nrd_pixel_contract_shader}")
        if(RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS)
            target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS=1)
            set(nrd_runtime_conversion_shader "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/shaders_NrdConversion_CSMain.cso")
            set(nrd_runtime_definitions)
            if(RTPBRSURVEY_NRD_SDK_DIAGNOSTICS)
                list(APPEND nrd_runtime_definitions RTPBRSURVEY_NRD_SDK_DIAGNOSTICS=1)
            endif()
            if(RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS)
                list(APPEND nrd_runtime_definitions RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS=1)
            endif()
            rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_NrdConversion.hlsl cs_6_0 CSMain
                DEFINITIONS ${nrd_runtime_definitions}
                INCLUDE_DIRS "${nrd_test_dir}"
                DEPENDS "${RTPBRSURVEY_ROOT}/Shaders/NrdRelaxInputConversion.hlsli"
                    "${nrd_test_dir}/NRDConfig.hlsli" "${nrd_test_dir}/NRD.hlsli")
            add_custom_target(RtPbrSurvey.NrdConversionShader DEPENDS "${nrd_runtime_conversion_shader}")
            add_dependencies(RtPbrSurvey.SceneRenderer RtPbrSurvey.NrdConversionShader)
        endif()
        find_program(RTPBRSURVEY_POWERSHELL NAMES pwsh powershell REQUIRED)
        add_test(NAME RtPbrSurvey.NrdCapturedConversion
            COMMAND "${RTPBRSURVEY_POWERSHELL}" -NoProfile -File
                "${RTPBRSURVEY_ROOT}/Tests/TestNrdCapturedConversion.ps1"
                -Exe "$<TARGET_FILE:RtPbrSurvey.PathTracingDenoiserSamplingTests>"
                -Shader "${nrd_captured_conversion_shader}" -ScratchRoot "${CMAKE_CURRENT_BINARY_DIR}/Tests/CapturedSmoke")
        if(RTPBRSURVEY_TEST_NRD_RECONSTRUCTION)
            set(RTPBRSURVEY_NRD_MATHLIB_DIR "${RTPBRSURVEY_NRD_SDK_DIR}/External/MathLib"
                CACHE PATH "Pinned MathLib v11 source for NRD reconstruction shader tests.")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_MATHLIB_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE mathlib_test_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE mathlib_test_result)
            if(NOT mathlib_test_result EQUAL 0 OR NOT mathlib_test_commit STREQUAL "aa432ad9a80691eb8c3ec4d576bbad5ac100fdc8")
                message(FATAL_ERROR "NRD reconstruction tests require the exact MathLib v11 source commit.")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SDK_DIR}" diff --quiet HEAD -- Shaders
                RESULT_VARIABLE nrd_reconstruction_dirty)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_MATHLIB_DIR}" diff --quiet HEAD
                RESULT_VARIABLE mathlib_reconstruction_dirty)
            if(NOT nrd_reconstruction_dirty EQUAL 0 OR NOT mathlib_reconstruction_dirty EQUAL 0)
                message(FATAL_ERROR "NRD reconstruction tests require unmodified SDK and MathLib shader sources.")
            endif()
            find_program(RTPBRSURVEY_POWERSHELL NAMES pwsh powershell REQUIRED)
            file(GLOB nrd_reconstruction_inputs CONFIGURE_DEPENDS "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/*.hlsli")
            file(GLOB mathlib_reconstruction_inputs CONFIGURE_DEPENDS "${RTPBRSURVEY_NRD_MATHLIB_DIR}/*.hlsli")
            if(RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS)
                target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS=1)
                target_sources(RtPbrSurvey.SceneRenderer PRIVATE Renderer/NrdReconstructionConstants.cpp)
                target_include_directories(RtPbrSurvey.SceneRenderer PRIVATE "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders")
                rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_NrdReconstruction.hlsl cs_6_0 main
                    INCLUDE_DIRS "${nrd_test_dir}" "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders" "${RTPBRSURVEY_NRD_MATHLIB_DIR}"
                    DEPENDS "${nrd_test_dir}/NRDConfig.hlsli" ${nrd_reconstruction_inputs} ${mathlib_reconstruction_inputs}
                        "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/RELAX_HitDistReconstruction.cs.hlsl")
                add_custom_target(RtPbrSurvey.NrdRuntimeReconstructionShader
                    DEPENDS "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/shaders_NrdReconstruction_main.cso")
                add_dependencies(RtPbrSurvey.SceneRenderer RtPbrSurvey.NrdRuntimeReconstructionShader)
                rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_NrdAdmission.hlsl cs_6_0 CSMain
                    DEPENDS "${RTPBRSURVEY_ROOT}/Shaders/NrdRelaxInputAdmission.hlsli")
                set(nrd_admission_vectors "${CMAKE_CURRENT_BINARY_DIR}/Tests/shaders_NrdAdmissionVectors_CSMain.cso")
                add_custom_command(OUTPUT "${nrd_admission_vectors}"
                    COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -ECSMain
                        "-Fo${nrd_admission_vectors}" "${RTPBRSURVEY_ROOT}/Tests/shaders_NrdAdmissionVectors.hlsl"
                    DEPENDS Tests/shaders_NrdAdmissionVectors.hlsl Shaders/NrdRelaxInputAdmission.hlsli
                    VERBATIM)
                add_custom_target(RtPbrSurvey.NrdAdmissionVectors DEPENDS "${nrd_admission_vectors}")
                add_custom_target(RtPbrSurvey.NrdAdmissionShader
                    DEPENDS "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/shaders_NrdAdmission_CSMain.cso")
                add_dependencies(RtPbrSurvey.SceneRenderer RtPbrSurvey.NrdAdmissionShader)
                if(RTPBRSURVEY_NRD_SDK_DIAGNOSTICS)
                    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_NrdComposition.hlsl cs_6_0 CSMain
                        DEPENDS "${RTPBRSURVEY_ROOT}/Shaders/NrdRelaxComposition.hlsli")
                    include(cmake/NrdSdkPlanning.cmake)
                endif()
            endif()
            set(nrd_reconstruction_shaders)
            foreach(border_mode IN ITEMS 0 1)
                set(nrd_reconstruction_shader "${nrd_test_dir}/RELAX_Reconstruction_mode${border_mode}.cso")
                add_custom_command(OUTPUT "${nrd_reconstruction_shader}"
                    COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -nologo -Tcs_6_0 -Emain
                        -DNRD_INTERNAL -DNRD_SIGNAL=3 -DNRD_MODE=0 "-DMODE_5X5=${border_mode}"
                        "-I${nrd_test_dir}" "-I${RTPBRSURVEY_NRD_MATHLIB_DIR}"
                        "-Fo${nrd_reconstruction_shader}"
                        "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/RELAX_HitDistReconstruction.cs.hlsl"
                    DEPENDS "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/RELAX_HitDistReconstruction.cs.hlsl"
                        "${nrd_test_dir}/NRDConfig.hlsli" ${nrd_reconstruction_inputs} ${mathlib_reconstruction_inputs}
                    VERBATIM)
                list(APPEND nrd_reconstruction_shaders "${nrd_reconstruction_shader}")
                add_test(NAME "RtPbrSurvey.NrdRelaxReconstructionContract${border_mode}"
                    COMMAND "${RTPBRSURVEY_POWERSHELL}" -NoProfile -File
                        "${RTPBRSURVEY_ROOT}/Tests/TestNrdReconstructionContract.ps1"
                        -Dxc "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe" -Shader "${nrd_reconstruction_shader}")
            endforeach()
            add_custom_target(RtPbrSurvey.NrdReconstructionShaders ALL DEPENDS ${nrd_reconstruction_shaders})
            add_executable(RtPbrSurvey.NrdReconstructionTests Tests/NrdReconstructionTests.cpp)
            target_compile_features(RtPbrSurvey.NrdReconstructionTests PRIVATE cxx_std_20)
            target_include_directories(RtPbrSurvey.NrdReconstructionTests PRIVATE "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders")
            target_compile_definitions(RtPbrSurvey.NrdReconstructionTests PRIVATE NOMINMAX)
            target_link_libraries(RtPbrSurvey.NrdReconstructionTests PRIVATE d3d12 dxgi)
            add_dependencies(RtPbrSurvey.NrdReconstructionTests RtPbrSurvey.NrdReconstructionShaders)
            add_test(NAME RtPbrSurvey.NrdReconstruction3x3
                COMMAND RtPbrSurvey.NrdReconstructionTests "${nrd_test_dir}/RELAX_Reconstruction_mode0.cso" 3x3)
            add_test(NAME RtPbrSurvey.NrdReconstruction5x5
                COMMAND RtPbrSurvey.NrdReconstructionTests "${nrd_test_dir}/RELAX_Reconstruction_mode1.cso" 5x5)
        endif()
    endif()

    add_executable(RtPbrSurvey.EvaluationStateTests
        Tests/EvaluationStateTests.cpp)
    target_link_libraries(RtPbrSurvey.EvaluationStateTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.EvaluationStateTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.EvaluationState COMMAND RtPbrSurvey.EvaluationStateTests)

    add_executable(RtPbrSurvey.DebugLineTests
        Tests/DebugLineTests.cpp)
    target_link_libraries(RtPbrSurvey.DebugLineTests PRIVATE RtPbrSurvey::SceneRenderer)
    add_test(NAME RtPbrSurvey.DebugLine COMMAND RtPbrSurvey.DebugLineTests)

    add_executable(RtPbrSurvey.DebugLineHostCompileProbe
        Tests/DebugLineHostCompileProbe.cpp)
    target_link_libraries(RtPbrSurvey.DebugLineHostCompileProbe PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.DebugLineHostCompileProbe PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.DebugLineHostCompileProbe COMMAND RtPbrSurvey.DebugLineHostCompileProbe)

    add_executable(RtPbrSurvey.CameraProjectionTests
        Tests/CameraProjectionTests.cpp)
    target_link_libraries(RtPbrSurvey.CameraProjectionTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.CameraProjectionTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.CameraProjection COMMAND RtPbrSurvey.CameraProjectionTests)

    add_executable(RtPbrSurvey.CameraViewTests
        Tests/CameraViewTests.cpp)
    target_link_libraries(RtPbrSurvey.CameraViewTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.CameraViewTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.CameraView COMMAND RtPbrSurvey.CameraViewTests)

    add_executable(RtPbrSurvey.SceneDocumentTests
        Tests/SceneDocumentTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneDocumentTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneDocumentTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneDocument COMMAND RtPbrSurvey.SceneDocumentTests)

    add_executable(RtPbrSurvey.SceneDocumentBuilderTests
        Tests/SceneDocumentBuilderTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneDocumentBuilderTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneDocumentBuilderTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneDocumentBuilder COMMAND RtPbrSurvey.SceneDocumentBuilderTests)

    add_executable(RtPbrSurvey.SceneEditorSessionTests
        Tests/SceneEditorSessionTests.cpp
        App/SceneEditorSession.cpp)
    target_link_libraries(RtPbrSurvey.SceneEditorSessionTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneEditorSessionTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneEditorSession COMMAND RtPbrSurvey.SceneEditorSessionTests)

    add_executable(RtPbrSurvey.RenderPresetStoreTests
        Tests/RenderPresetStoreTests.cpp)
    target_link_libraries(RtPbrSurvey.RenderPresetStoreTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.RenderPresetStoreTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.RenderPresetStore COMMAND RtPbrSurvey.RenderPresetStoreTests)

    add_executable(RtPbrSurvey.DebugCameraControllerTests
        Tests/DebugCameraControllerTests.cpp)
    target_link_libraries(RtPbrSurvey.DebugCameraControllerTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.DebugCameraControllerTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.DebugCameraController COMMAND RtPbrSurvey.DebugCameraControllerTests)

    add_executable(RtPbrSurvey.SceneRendererDebugUiApiTests
        Tests/SceneRendererDebugUiApiTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneRendererDebugUiApiTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneRendererDebugUiApiTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneRendererDebugUiApi COMMAND RtPbrSurvey.SceneRendererDebugUiApiTests)

    add_executable(RtPbrSurvey.SceneBuilderTextureTests
        Tests/SceneBuilderTextureTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneBuilderTextureTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneBuilderTextureTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneBuilderTexture COMMAND RtPbrSurvey.SceneBuilderTextureTests)

    add_executable(RtPbrSurvey.SceneBuilderMeshTests
        Tests/SceneBuilderMeshTests.cpp)
    target_link_libraries(RtPbrSurvey.SceneBuilderMeshTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.SceneBuilderMeshTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.SceneBuilderMesh COMMAND RtPbrSurvey.SceneBuilderMeshTests)

    add_executable(RtPbrSurvey.EmissiveTriangleTests Tests/EmissiveTriangleTests.cpp)
    target_link_libraries(RtPbrSurvey.EmissiveTriangleTests PRIVATE RtPbrSurvey::SceneRenderer)
    add_test(NAME RtPbrSurvey.EmissiveTriangle COMMAND RtPbrSurvey.EmissiveTriangleTests)

    add_executable(RtPbrSurvey.GltfNodeMeshTests
        Tests/GltfNodeMeshTests.cpp)
    target_link_libraries(RtPbrSurvey.GltfNodeMeshTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.GltfNodeMeshTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.GltfNodeMesh COMMAND RtPbrSurvey.GltfNodeMeshTests)

    add_executable(RtPbrSurvey.TextureMipChainTests
        Tests/TextureMipChainTests.cpp)
    target_link_libraries(RtPbrSurvey.TextureMipChainTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.TextureMipChainTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.TextureMipChain COMMAND RtPbrSurvey.TextureMipChainTests)

    add_executable(RtPbrSurvey.ScreenshotTests
        Tests/ScreenshotTests.cpp)
    target_link_libraries(RtPbrSurvey.ScreenshotTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.ScreenshotTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.Screenshot COMMAND RtPbrSurvey.ScreenshotTests)

    add_executable(RtPbrSurvey.ScreenshotRequestQueueTests
        Tests/ScreenshotRequestQueueTests.cpp)
    target_link_libraries(RtPbrSurvey.ScreenshotRequestQueueTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.ScreenshotRequestQueueTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.ScreenshotRequestQueue COMMAND RtPbrSurvey.ScreenshotRequestQueueTests)

    add_executable(RtPbrSurvey.CaptureSessionTests
        Tests/CaptureSessionTests.cpp)
    target_link_libraries(RtPbrSurvey.CaptureSessionTests PRIVATE RtPbrSurvey::SceneRenderer)
    rtpbrsurvey_copy_runtime_files(RtPbrSurvey.CaptureSessionTests)
    if(MSVC)
        target_compile_options(RtPbrSurvey.CaptureSessionTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.CaptureSession COMMAND RtPbrSurvey.CaptureSessionTests)

    add_executable(RtPbrSurvey.Mp4EncoderTests Tests/Mp4EncoderTests.cpp)
    target_link_libraries(RtPbrSurvey.Mp4EncoderTests PRIVATE RtPbrSurvey::SceneRenderer)
    rtpbrsurvey_copy_runtime_files(RtPbrSurvey.Mp4EncoderTests)
    if(MSVC)
        target_compile_options(RtPbrSurvey.Mp4EncoderTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.Mp4Encoder COMMAND RtPbrSurvey.Mp4EncoderTests)

    add_executable(RtPbrSurvey.Mp4EncoderWorkerTests Tests/Mp4EncoderWorkerTests.cpp)
    target_link_libraries(RtPbrSurvey.Mp4EncoderWorkerTests PRIVATE RtPbrSurvey::SceneRenderer)
    rtpbrsurvey_copy_runtime_files(RtPbrSurvey.Mp4EncoderWorkerTests)
    add_test(NAME RtPbrSurvey.Mp4EncoderWorker COMMAND RtPbrSurvey.Mp4EncoderWorkerTests)
    set_tests_properties(RtPbrSurvey.Mp4EncoderWorker PROPERTIES TIMEOUT 30)

    add_executable(RtPbrSurvey.AssetPathTests
        Tests/AssetPathTests.cpp)
    target_link_libraries(RtPbrSurvey.AssetPathTests PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey.AssetPathTests PRIVATE /permissive)
    endif()
    add_test(NAME RtPbrSurvey.AssetPath COMMAND RtPbrSurvey.AssetPathTests)
endif()
