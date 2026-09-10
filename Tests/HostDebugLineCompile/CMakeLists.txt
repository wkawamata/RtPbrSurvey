cmake_minimum_required(VERSION 3.23)

project(RtPbrSurveyHostDebugLineCompile LANGUAGES CXX)

if(NOT RTPBRSURVEY_SOURCE_DIR)
    message(FATAL_ERROR "Set RTPBRSURVEY_SOURCE_DIR to the RtPbrSurvey repository root.")
endif()

set(RTPBRSURVEY_BUILD_APP OFF CACHE BOOL "" FORCE)
set(RTPBRSURVEY_BUILD_SHADERS ON CACHE BOOL "" FORCE)
set(RTPBRSURVEY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${RTPBRSURVEY_SOURCE_DIR}" RtPbrSurvey)

add_executable(RtPbrSurveyHostDebugLineCompile
    ../DebugLineHostCompileProbe.cpp)
target_link_libraries(RtPbrSurveyHostDebugLineCompile PRIVATE RtPbrSurvey::SceneRenderer)
if(MSVC)
    target_compile_options(RtPbrSurveyHostDebugLineCompile PRIVATE /permissive)
endif()
