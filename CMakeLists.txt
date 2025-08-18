cmake_minimum_required(VERSION 3.10)
project(SmartGridServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(SOURCES
    server/main.cpp
    server/server.cpp
    server/aes_manager.cpp
    server/power_processor.cpp
    server/thread_pool.cpp
    server/logger.cpp
)

add_executable(smart_grid_server ${SOURCES})

target_include_directories(smart_grid_server PRIVATE
    server/
    common/
    ${OPENSSL_INCLUDE_DIRS}
)

target_link_libraries(smart_grid_server
    ${OPENSSL_LIBRARIES}
    pthread
)

target_compile_options(smart_grid_server PRIVATE
    -O3
    ${OPENSSL_CFLAGS_OTHER}
)
