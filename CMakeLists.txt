cmake_minimum_required(VERSION 3.22)

# Plugins built from here need macOS 11+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Numinos VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd / editor autocomplete

option(NUMINOS_BUILD_PLUGIN "Build the DAW plugin (downloads JUCE on first configure)" ON)

add_subdirectory(numins) # the DSP — this is where you live
add_subdirectory(tools)  # numin-render, numin-check

if(NUMINOS_BUILD_PLUGIN)
    # Keep downloads in .deps/ so every build folder shares one copy of JUCE.
    set(FETCHCONTENT_BASE_DIR ${PROJECT_SOURCE_DIR}/.deps)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
    add_subdirectory(plugin)
endif()
