cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME shader_compiler)

project(${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(EXECUTABLE_OUTPUT_PATH "../")

add_compile_options(
    -O2
)

link_directories(
    /Users/samuliak/Documents/spirv-cross/build
)

add_executable(${PROJECT_NAME}
    shader_compiler.cpp
)

include_directories(
    "external/json/include"
    "/Users/samuliak/Documents/spirv-cross"
)

#find_package(spirv_cross_shared)

#find_library(
#    SPIRV_CROSS_LIB
#    NAMES lspirv-cross-cpp libspirv-cross-cpp
#    HINTS "/Users/samuliak/Documents/spirv-cross/build"
#    NO_DEFAULT_PATH
#)

target_link_libraries(${PROJECT_NAME}
    #spirv-cross-shared
    #${SPIRV_CROSS_LIB}
    -lspirv-cross-cpp -lspirv-cross-msl -lspirv-cross-glsl -lspirv-cross-core
)
