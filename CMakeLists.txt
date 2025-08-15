cmake_minimum_required(VERSION 3.10)
project(SmartGridServer)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)

find_package(nlohmann_json REQUIRED)

add_executable(smart_grid_server main.cpp)

target_link_libraries(smart_grid_server
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

target_include_directories(smart_grid_server PRIVATE ${OPENSSL_INCLUDE_DIRS})
target_compile_options(smart_grid_server PRIVATE ${OPENSSL_CFLAGS_OTHER})
