cmake_minimum_required(VERSION 3.10)
project(V8CppProject)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -D_MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_MT")
string(REPLACE "-D_DLL" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "-D_DLL" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -D_MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_MT")
string(REPLACE "-D_DLL" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REPLACE "-D_DLL" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

# Print CMake version
message(STATUS "CMAKE_VERSION: ${CMAKE_VERSION}")

# Print compiler info
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(V8_ROOT "H:\\v8_build\\v8")

include_directories(
        ${V8_ROOT}/include
        ${CMAKE_SOURCE_DIR}/extern
)

link_directories(
        $<$<CONFIG:Debug>:${V8_ROOT}/out_clang/x64.debug/obj>
        $<$<CONFIG:Release>:${V8_ROOT}/out_clang/x64.release/obj>
        $<$<CONFIG:RelWithDebInfo>:${V8_ROOT}/out_clang/x64.release/obj>
        $<$<CONFIG:MinSizeRel>:${V8_ROOT}/out_clang/x64.release/obj>
)

function(apply_v8_settings target)
    target_compile_definitions(${target} PRIVATE
            V8_COMPRESS_POINTERS
            V8_31BIT_SMIS_ON_64BIT_ARCH
            V8_ENABLE_SANDBOX
            V8_SANDBOX_IS_AVAILABLE
    )

    # Link against V8 libraries
    target_link_libraries(${target} PRIVATE
            v8_monolith
            winmm
            dbghelp
    )
endfunction()

add_executable(v8_cpp_test
        src/example_usage.cpp
)

add_executable(v8_cpp_benchmark
        src/benchmark.cpp
)

apply_v8_settings(v8_cpp_test)
apply_v8_settings(v8_cpp_benchmark)
# Copy test.js to the build directory
configure_file(${CMAKE_SOURCE_DIR}/java_script/test.js ${CMAKE_BINARY_DIR}/test.js COPYONLY)