if(NOT DEFINED RTPBRSURVEY_SHADER_SOURCE OR NOT EXISTS "${RTPBRSURVEY_SHADER_SOURCE}")
    message(FATAL_ERROR "The renderer shader source list could not be read: ${RTPBRSURVEY_SHADER_SOURCE}")
endif()

if(NOT DEFINED RTPBRSURVEY_RUNTIME_DIR OR NOT IS_DIRECTORY "${RTPBRSURVEY_RUNTIME_DIR}")
    message(FATAL_ERROR "The host runtime directory does not exist: ${RTPBRSURVEY_RUNTIME_DIR}")
endif()

file(READ "${RTPBRSURVEY_SHADER_SOURCE}" shader_source)
string(REGEX MATCHALL "[A-Za-z0-9_]+\\.cso" required_shaders "${shader_source}")
list(REMOVE_DUPLICATES required_shaders)

if(NOT required_shaders)
    message(FATAL_ERROR "No runtime shader references were found in ${RTPBRSURVEY_SHADER_SOURCE}")
endif()

foreach(shader IN LISTS required_shaders)
    if(NOT EXISTS "${RTPBRSURVEY_RUNTIME_DIR}/${shader}")
        message(FATAL_ERROR
            "Required runtime shader is missing: ${RTPBRSURVEY_RUNTIME_DIR}/${shader}. "
            "Add its HLSL entry point to RTPBRSURVEY_SHADER_OUTPUTS.")
    endif()
endforeach()

list(LENGTH required_shaders required_shader_count)
message(STATUS "Validated ${required_shader_count} runtime shaders in ${RTPBRSURVEY_RUNTIME_DIR}")
