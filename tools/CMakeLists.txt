# Command-line tools. JUCE-free, so they build fast.
add_executable(numin-render render.cpp)
target_link_libraries(numin-render PRIVATE numins)
target_include_directories(numin-render PRIVATE ${PROJECT_SOURCE_DIR}/third_party)

add_executable(numin-check check.cpp)
target_link_libraries(numin-check PRIVATE numins)
