# The DSP library. No JUCE in here — just C++.
add_library(numins STATIC Registry.cpp)
target_include_directories(numins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(numins PUBLIC cxx_std_17)
set_target_properties(numins PROPERTIES POSITION_INDEPENDENT_CODE ON) # linked into plugin .so/.dylib
target_compile_definitions(numins PUBLIC _USE_MATH_DEFINES) # M_PI on Windows

# Headers listed so they show up in Xcode / IDEs.
file(GLOB NUMIN_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/*.h)
target_sources(numins PRIVATE Numin.h Registry.h ${NUMIN_HEADERS})
