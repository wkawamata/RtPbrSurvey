if(NOT DEFINED RTPBRSURVEY_DUMPBIN OR RTPBRSURVEY_DUMPBIN STREQUAL "")
    message(FATAL_ERROR "CMAKE_DUMPBIN is required to validate the Streamline host import table.")
endif()

if(NOT DEFINED RTPBRSURVEY_HOST_EXECUTABLE OR NOT EXISTS "${RTPBRSURVEY_HOST_EXECUTABLE}")
    message(FATAL_ERROR "The Streamline host executable does not exist: ${RTPBRSURVEY_HOST_EXECUTABLE}")
endif()

execute_process(
    COMMAND "${RTPBRSURVEY_DUMPBIN}" /DEPENDENTS "${RTPBRSURVEY_HOST_EXECUTABLE}"
    RESULT_VARIABLE dumpbin_result
    OUTPUT_VARIABLE dumpbin_output
    ERROR_VARIABLE dumpbin_error)

if(NOT dumpbin_result EQUAL 0)
    message(FATAL_ERROR
        "dumpbin failed while validating the Streamline host imports (${dumpbin_result}):\n${dumpbin_error}")
endif()

string(TOLOWER "${dumpbin_output}" dependencies)

if(NOT dependencies MATCHES "sl\\.interposer\\.dll")
    message(FATAL_ERROR
        "Streamline SDK is enabled, but the host does not import sl.interposer.dll:\n${dumpbin_output}")
endif()

foreach(platform_dll IN ITEMS d3d12.dll dxgi.dll d3d11.dll)
    if(dependencies MATCHES "(^|[\r\n\t ])${platform_dll}([\r\n\t ]|$)")
        message(FATAL_ERROR
            "Streamline SDK is enabled, but the host directly imports ${platform_dll}. "
            "The interposer must own the platform entry points:\n${dumpbin_output}")
    endif()
endforeach()

message(STATUS "Validated Streamline host imports: ${RTPBRSURVEY_HOST_EXECUTABLE}")
