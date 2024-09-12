cmake_minimum_required(VERSION 3.10)
project(V8CppProject)
set(CMAKE_GENERATOR_PLATFORM x64)

# Print CMake version
message(STATUS "CMAKE_VERSION: ${CMAKE_VERSION}")

# Print compiler info
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_compile_options(/std:c++20 /Zc:__cplusplus)
    # Set runtime library to static
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:>")
    # Add MT flag
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    message(STATUS "MSVC: ${MSVC_VERSION}")
else()
    add_compile_options(-std=c++20)
endif()

# V8 library path (adjust these paths to your local V8 build)
set(V8_ROOT "H:\\v8_build\\v8")

include_directories(
        ${V8_ROOT}/include
)

link_directories(
        ${V8_ROOT}/out.gn/x64.release/obj
        ${V8_ROOT}/out.gn/x64.release/obj/third_party/icu
)

# Add your source files
add_executable(v8_cpp_test
        src/main.cpp
)

# Include directory
target_include_directories(v8_cpp_test PRIVATE include)
target_compile_features(v8_cpp_test PRIVATE cxx_std_20)

target_compile_definitions(v8_cpp_test PRIVATE
        V8_COMPRESS_POINTERS
        V8_31BIT_SMIS_ON_64BIT_ARCH
        V8_ENABLE_SANDBOX
        V8_SANDBOX_IS_AVAILABLE
)

# Link against V8 libraries
target_link_libraries(v8_cpp_test
        v8_monolith
        winmm.lib
        dbghelp.lib
)

# Copy test.js to the build directory
configure_file(${CMAKE_SOURCE_DIR}/java_script/test.js ${CMAKE_BINARY_DIR}/test.js COPYONLY)