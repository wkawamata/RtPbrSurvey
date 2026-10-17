set(RTPBRSURVEY_NRD_SHADERMAKE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/nrd-shadermake-src"
    CACHE PATH "ShaderMake checkout pinned by NRD v4.17.3.")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SHADERMAKE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE nrd_shadermake_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE nrd_shadermake_result)
if(NOT nrd_shadermake_result EQUAL 0 OR NOT nrd_shadermake_commit STREQUAL "18f5a344e7ca8fa65daaf079d07bc8ce38453e05")
    message(FATAL_ERROR "NRD SDK diagnostics require the pinned ShaderMake checkout; see the denoiser input contract.")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SDK_DIR}" diff --quiet HEAD -- Source Include Shaders
    RESULT_VARIABLE nrd_sdk_modified)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RTPBRSURVEY_NRD_SHADERMAKE_DIR}" diff --quiet HEAD -- ShaderMake/ShaderBlob.h
    RESULT_VARIABLE nrd_shadermake_modified)
if(NOT nrd_sdk_modified EQUAL 0 OR NOT nrd_shadermake_modified EQUAL 0)
    message(FATAL_ERROR "NRD SDK diagnostics require unmodified SDK sources and ShaderMake headers.")
endif()

# Compile the upstream CPU scheduler without embedding GPU bytecode. The upstream
# shader config include resolves to the existing pinned contract configuration.
add_library(RtPbrSurvey.NrdSdk STATIC
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/InstanceImpl.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Reblur.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Reference.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Relax.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Sigma.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Timer.cpp"
    "${RTPBRSURVEY_NRD_SDK_DIR}/Source/Wrapper.cpp")
target_compile_features(RtPbrSurvey.NrdSdk PRIVATE cxx_std_17)
target_include_directories(RtPbrSurvey.NrdSdk PRIVATE
    "${RTPBRSURVEY_NRD_SDK_DIR}/Include" "${RTPBRSURVEY_NRD_MATHLIB_DIR}"
    "${RTPBRSURVEY_NRD_SHADERMAKE_DIR}" "${nrd_test_dir}")
target_compile_definitions(RtPbrSurvey.NrdSdk PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    NRD_STATIC_LIBRARY=1 NRD_EMBEDS_DXIL_SHADERS=0 NRD_EMBEDS_DXBC_SHADERS=0 NRD_EMBEDS_SPIRV_SHADERS=0
    SPIRV_SREG_OFFSET=0 SPIRV_BREG_OFFSET=2 SPIRV_UREG_OFFSET=3 SPIRV_TREG_OFFSET=20)
target_sources(RtPbrSurvey.SceneRenderer PRIVATE Renderer/NrdRelaxSdkAdapter.cpp)
target_include_directories(RtPbrSurvey.SceneRenderer PRIVATE "${RTPBRSURVEY_NRD_SDK_DIR}/Include")
target_link_libraries(RtPbrSurvey.SceneRenderer PRIVATE RtPbrSurvey.NrdSdk)
add_executable(RtPbrSurvey.NrdSdkAdapterTests Tests/NrdSdkAdapterTests.cpp)
target_link_libraries(RtPbrSurvey.NrdSdkAdapterTests PRIVATE RtPbrSurvey::SceneRenderer)
add_test(NAME RtPbrSurvey.NrdSdkAdapter COMMAND RtPbrSurvey.NrdSdkAdapterTests)

add_executable(RtPbrSurvey.NrdShaderManifest Tools/NrdShaderManifest.cpp)
target_include_directories(RtPbrSurvey.NrdShaderManifest PRIVATE "${RTPBRSURVEY_NRD_SDK_DIR}/Include")
target_compile_definitions(RtPbrSurvey.NrdShaderManifest PRIVATE NRD_STATIC_LIBRARY=1)
target_link_libraries(RtPbrSurvey.NrdShaderManifest PRIVATE RtPbrSurvey.NrdSdk)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB nrd_runtime_shader_sources CONFIGURE_DEPENDS "${RTPBRSURVEY_NRD_SDK_DIR}/Shaders/*.hlsl")
set(nrd_runtime_outputs "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/nrd-relax-manifest.txt")
foreach(pipeline_index RANGE 0 26)
    list(APPEND nrd_runtime_outputs "${RTPBRSURVEY_SHADER_OUTPUT_DIR}/nrd-relax-${pipeline_index}.cso")
endforeach()
add_custom_command(OUTPUT ${nrd_runtime_outputs}
    COMMAND "${Python3_EXECUTABLE}" "${RTPBRSURVEY_ROOT}/Tools/compile_nrd_shaders.py"
        --manifest-tool "$<TARGET_FILE:RtPbrSurvey.NrdShaderManifest>"
        --dxc "${RTPBRSURVEY_DXC_NATIVE_DIR}/bin/x64/dxc.exe"
        --sdk "${RTPBRSURVEY_NRD_SDK_DIR}" --mathlib "${RTPBRSURVEY_NRD_MATHLIB_DIR}"
        --config "${nrd_test_dir}" --output "${RTPBRSURVEY_SHADER_OUTPUT_DIR}"
    DEPENDS RtPbrSurvey.NrdShaderManifest Tools/compile_nrd_shaders.py
        ${nrd_runtime_shader_sources} ${nrd_reconstruction_inputs} ${mathlib_reconstruction_inputs}
        "${nrd_test_dir}/NRDConfig.hlsli"
    VERBATIM)
add_custom_target(RtPbrSurvey.NrdRuntimeShaders
    DEPENDS ${nrd_runtime_outputs})
add_dependencies(RtPbrSurvey.SceneRenderer RtPbrSurvey.NrdRuntimeShaders)
target_compile_definitions(RtPbrSurvey.SceneRenderer PUBLIC RTPBRSURVEY_NRD_SDK_DIAGNOSTICS=1)
target_sources(RtPbrSurvey.SceneRenderer PRIVATE Renderer/NrdRelaxD3D12.cpp)
