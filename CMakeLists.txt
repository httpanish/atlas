cmake_minimum_required(VERSION 3.20)
project(atlas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Core Atlas library
add_library(atlas
    src/app.cpp
    src/parser.cpp
    src/serializer.cpp
    src/socket.cpp
    src/server.cpp
)

target_include_directories(atlas PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Example executable (HTTP pipeline demo)
add_executable(atlas_example
    examples/main.cpp
)
target_link_libraries(atlas_example PRIVATE atlas)

# Server executable (TCP networking demo)
add_executable(atlas_server
    examples/server_main.cpp
)
target_link_libraries(atlas_server PRIVATE atlas)

# Test executable
add_executable(atlas_tests
    tests/test_basic.cpp
)
target_link_libraries(atlas_tests PRIVATE atlas)

enable_testing()
add_test(NAME basic_tests COMMAND atlas_tests)
