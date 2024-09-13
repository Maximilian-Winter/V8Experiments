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
)

link_directories(
        ${V8_ROOT}/out_clang/x64.release/obj
        ${V8_ROOT}/out_clang/x64.release/obj/third_party/icu
)

# Add your source files
add_executable(v8_cpp_test
        src/main.cpp
)


target_compile_definitions(v8_cpp_test PRIVATE
        V8_COMPRESS_POINTERS
        V8_31BIT_SMIS_ON_64BIT_ARCH
        V8_ENABLE_SANDBOX
        V8_SANDBOX_IS_AVAILABLE
)

# Link against V8 libraries
target_link_libraries(v8_cpp_test
        v8_monolith
        winmm
        dbghelp
)

# Copy test.js to the build directory
configure_file(${CMAKE_SOURCE_DIR}/java_script/test.js ${CMAKE_BINARY_DIR}/test.js COPYONLY)