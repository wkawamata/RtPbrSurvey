cmake_minimum_required(VERSION 3.23)

project(RtPbrSurvey LANGUAGES CXX)

option(RTPBRSURVEY_BUILD_APP "Build the standalone RtPbrSurvey executable." OFF)
option(RTPBRSURVEY_BUILD_SHADERS "Build RtPbrSurvey runtime shaders." ON)

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
    Engine/RtPbrSurveyEngine.cpp
    Engine/RtPbrSurveyEnginePasses.cpp
    Platform/CommandLineOptions.cpp
    Platform/Win32Application.cpp
    Renderer/AccelerationStructureResources.cpp
    Renderer/BrdfLut.cpp
    Renderer/ClearPass.cpp
    Renderer/DebugDumpCapture.cpp
    Renderer/DebugDumpReport.cpp
    Renderer/DebugLinePass.cpp
    Renderer/EnvironmentMap.cpp
    Renderer/FullscreenTriangle.cpp
    Renderer/GBuffer.cpp
    Renderer/HdrOutput.cpp
    Renderer/HybridReflectionPass.cpp
    Renderer/LightingPass.cpp
    Renderer/MaterialBuffer.cpp
    Renderer/PipelineFactory.cpp
    Renderer/RayQueryShadowPass.cpp
    Renderer/RayQueryTlasDebugPass.cpp
    Renderer/RayTracingSupport.cpp
    Renderer/ReflectionEvaluatePass.cpp
    Renderer/ReflectionRayHitDebugPass.cpp
    Renderer/RootSignatureFactory.cpp
    Renderer/SceneGeometryPass.cpp
    Renderer/ShadowMaskDebugPass.cpp
    Renderer/SpecularDebugRayQueryPass.cpp
    Renderer/StagedDescriptorAllocator_Test.cpp
    Renderer/StreamlineAdapter.cpp
    Renderer/TemporalUpscalerSupport.cpp
    Renderer/ToneMap.cpp
    Runtime/SceneRenderer.cpp
    Scene/ProceduralSceneBuilder.cpp
    Scene/SceneBuilder.cpp
    Scene/SceneFactory.cpp
    Scene/SampleScene.cpp
    Ui/ImGuiSystem.cpp
    stdafx.cpp
)

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

if(EXISTS "${RTPBRSURVEY_STREAMLINE_SDK_DIR}/include/sl.h")
    target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_HAS_STREAMLINE_SDK)
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
        d3d12
        dxgi
        d3dcompiler
        dxguid
        delayimp)

if(EXISTS "${RTPBRSURVEY_DXC_NATIVE_DIR}/lib/x64/dxcompiler.lib")
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC "${RTPBRSURVEY_DXC_NATIVE_DIR}/lib/x64/dxcompiler.lib")
endif()

if(EXISTS "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}/bin/x64/WinPixEventRuntime.lib")
    target_link_libraries(RtPbrSurvey.SceneRenderer PUBLIC "${RTPBRSURVEY_WINPIX_PACKAGE_DIR}/bin/x64/WinPixEventRuntime.lib")
endif()

if(MSVC)
    target_compile_options(RtPbrSurvey.SceneRenderer PRIVATE /permissive)
    target_link_options(RtPbrSurvey.SceneRenderer PUBLIC /DELAYLOAD:d3d12.dll)
endif()

function(rtpbrsurvey_add_shader output_var shader_file stage entry_point)
    get_filename_component(shader_name "${shader_file}" NAME_WE)
    set(output_file "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/${shader_name}_${entry_point}.cso")
    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RTPBRSURVEY_SHADER_OUTPUT_DIR}"
        COMMAND "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe"
            -nologo "-T${stage}" "-E${entry_point}" -Zi -Qembed_debug
            "-Fo${output_file}" "${RTPBRSURVEY_ROOT}/${shader_file}"
        DEPENDS "${RTPBRSURVEY_ROOT}/${shader_file}"
        WORKING_DIRECTORY "${RTPBRSURVEY_ROOT}"
        VERBATIM)
    set(${output_var} "${${output_var}};${output_file}" PARENT_SCOPE)
endfunction()

if(RTPBRSURVEY_BUILD_SHADERS)
    set(RTPBRSURVEY_SHADER_OUTPUTS)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DepthOnlyVS.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBuffer.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_GBuffer.hlsl ps_6_0 PSMain)
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
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ToneMap.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ToneMap.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugLine.hlsl vs_6_0 VSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_DebugLine.hlsl ps_6_0 PSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_ProceduralEnvMap.hlsl cs_6_0 CSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_RayQueryShadow.hlsl cs_6_5 CSMain)
    rtpbrsurvey_add_shader(RTPBRSURVEY_SHADER_OUTPUTS Shaders/shaders_HybridReflection.hlsl cs_6_5 CSMain)
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
        App/RtPbrSurveyApp.cpp
        App/SceneConfig.cpp
        App/SceneSelectUi.cpp)
    target_link_libraries(RtPbrSurvey PRIVATE RtPbrSurvey::SceneRenderer)
    if(MSVC)
        target_compile_options(RtPbrSurvey PRIVATE /permissive)
    endif()
    rtpbrsurvey_copy_runtime_files(RtPbrSurvey)
endif()
