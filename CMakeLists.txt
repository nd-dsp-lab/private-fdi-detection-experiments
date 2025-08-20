cmake_minimum_required(VERSION 3.16)
project(SmartGridServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

# Common server library
add_library(server_lib
    server/aes_manager.cpp
    server/logger.cpp
    server/power_processor.cpp
    server/server.cpp
    server/thread_pool.cpp
)

target_include_directories(server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server_lib OpenSSL::SSL OpenSSL::Crypto pthread)

# Original server
add_executable(smart_grid_server server/main.cpp)
target_link_libraries(smart_grid_server server_lib)

# SGX server
add_executable(smart_grid_server_sgx server_sgx/main.cpp)
target_link_libraries(smart_grid_server_sgx server_lib)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_link_options(-pie)  