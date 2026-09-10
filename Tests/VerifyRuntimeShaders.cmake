cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED RTPBRSURVEY_ROOT)
    message(FATAL_ERROR "RTPBRSURVEY_ROOT is required")
endif()

file(READ "${RTPBRSURVEY_ROOT}/Engine/RtPbrSurveyEngine.cpp" engine_source)
string(REGEX MATCHALL "LoadShaderBytecode\\(L\"[^\"]+\\.cso\"\\)" runtime_calls "${engine_source}")

set(runtime_shader_names)
foreach(runtime_call IN LISTS runtime_calls)
    string(REGEX REPLACE ".*L\"([^\"]+)\".*" "\\1" shader_name "${runtime_call}")
    list(APPEND runtime_shader_names "${shader_name}")
endforeach()
list(REMOVE_DUPLICATES runtime_shader_names)

file(READ "${RTPBRSURVEY_ROOT}/CMakeLists.txt" cmake_source)
string(REGEX MATCHALL
    "rtpbrsurvey_add_shader\\(RTPBRSURVEY_SHADER_OUTPUTS[ \t]+Shaders/[^ \t\r\n]+[ \t]+[^ \t\r\n]+[ \t]+[^ \t\r\n\\)]+\\)"
    shader_rules "${cmake_source}")

set(generated_shader_names)
foreach(shader_rule IN LISTS shader_rules)
    string(REGEX REPLACE
        ".*Shaders/([^/ \t\r\n]+)\\.hlsl[ \t]+[^ \t\r\n]+[ \t]+([^ \t\r\n\\)]+)\\).*"
        "\\1_\\2.cso" shader_name "${shader_rule}")
    list(APPEND generated_shader_names "${shader_name}")
endforeach()

set(missing_shader_names)
foreach(shader_name IN LISTS runtime_shader_names)
    if(NOT shader_name IN_LIST generated_shader_names)
        list(APPEND missing_shader_names "${shader_name}")
    endif()
endforeach()

if(missing_shader_names)
    list(JOIN missing_shader_names ", " missing_shader_text)
    message(FATAL_ERROR "Runtime shaders missing from RTPBRSURVEY_SHADER_OUTPUTS: ${missing_shader_text}")
endif()

list(LENGTH runtime_shader_names runtime_shader_count)
message(STATUS "Verified ${runtime_shader_count} runtime shader references")
