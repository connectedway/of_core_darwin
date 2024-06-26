cmake_minimum_required(VERSION 3.20.0)
project(of_core_darwin VERSION 1.0.1 DESCRIPTION "OpenFiles Platform for Darwin")

include(configs/default)
configure_file(include/ofc_darwin/config.h.in ofc_darwin/config.h @ONLY)
include_directories(
        ${of_core_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${of_core_darwin_SOURCE_DIR}/include
)

set(SRCS
        src/backtrace_darwin.c
        src/console_darwin.c
        src/env_darwin.c
        src/event_darwin.c
        src/lock_darwin.c
        src/net_darwin.c
        src/process_darwin.c
        src/socket_darwin.c
        src/thread_darwin.c
        src/time_darwin.c
        src/waitset_darwin.c
        )

add_library(of_core_darwin OBJECT ${SRCS})
set_property(TARGET of_core_darwin PROPERTY POSITION_INDEPENDENT_CODE ON)

