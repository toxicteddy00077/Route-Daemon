cmake_minimum_required(VERSION 3.16)

project(route-daemon
    VERSION 0.1.0
    DESCRIPTION "Performant C router daemon"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wwrite-strings
    -Wunused
    -Wmissing-prototypes
    -Wstrict-prototypes)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)

set(DAEMON_SOURCES
    src/main.c
    src/log.c
    src/config.c
    src/daemon.c)

add_executable(route-daemon ${DAEMON_SOURCES})

target_include_directories(route-daemon PRIVATE include)

target_compile_definitions(route-daemon PRIVATE
    DAEMON_VERSION="${PROJECT_VERSION}"
    CONFIG_DIR="/etc/route-daemon"
    STATE_DIR="/var/lib/route-daemon"
    LOG_DIR="/var/log/route-daemon"
    PID_FILE="/run/route-daemon.pid")

target_link_libraries(route-daemon PRIVATE PkgConfig::JSONC)

enable_testing()
add_subdirectory(tests)
