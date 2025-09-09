# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(SmartGridServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(JSON REQUIRED nlohmann_json)

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Server library
add_library(server_lib
    server/aes_manager.cpp
    server/server.cpp
    server/logger.cpp
    server/power_processor.cpp
    server/thread_pool.cpp
)

target_include_directories(server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JSON_INCLUDE_DIRS}
)

target_link_libraries(server_lib
    OpenSSL::Crypto
    pthread
    ${JSON_LIBRARIES}
)

# Native server executable
add_executable(smart_grid_server
    server/main.cpp
)

target_link_libraries(smart_grid_server server_lib)

# SGX server executable
add_executable(smart_grid_server_sgx
    server_sgx/main.cpp
)

target_link_libraries(smart_grid_server_sgx server_lib)
