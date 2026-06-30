if(NOT DEFINED MBOOT_SOURCE_DIR OR NOT DEFINED MBOOT_BINARY_DIR OR NOT DEFINED MBOOT_GENERATOR OR NOT DEFINED MBOOT_C_COMPILER OR NOT DEFINED MBOOT_C_COMPILER_ID)
    message(FATAL_ERROR "MBOOT_SOURCE_DIR, MBOOT_BINARY_DIR, MBOOT_GENERATOR, MBOOT_C_COMPILER, and MBOOT_C_COMPILER_ID are required")
endif()
if(NOT DEFINED MBOOT_CONFIG OR MBOOT_CONFIG STREQUAL "")
    set(MBOOT_CONFIG "Debug")
endif()

set(_root "${MBOOT_BINARY_DIR}/install-consumer")
set(_prefix "${_root}/prefix")
set(_consumer_src "${_root}/consumer-src")
set(_consumer_build "${_root}/consumer-build")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_consumer_src}")
file(MAKE_DIRECTORY "${_consumer_build}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${MBOOT_BINARY_DIR}" --prefix "${_prefix}" --config "${MBOOT_CONFIG}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "install failed\n${install_stdout}\n${install_stderr}")
endif()

file(WRITE "${_consumer_src}/CMakeLists.txt" [[
cmake_minimum_required(VERSION 3.20)
project(microboot_consumer C)
find_package(microboot CONFIG REQUIRED)
add_executable(consumer consumer.c)
target_link_libraries(consumer PRIVATE microboot::microboot)
if(MSVC)
    target_compile_options(consumer PRIVATE /W4 /WX /permissive-)
elseif("@MBOOT_C_COMPILER_ID@" STREQUAL "Clang")
    target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-command-line-argument)
else()
    target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
]])
file(READ "${_consumer_src}/CMakeLists.txt" _consumer_cmakelists)
string(REPLACE "@MBOOT_C_COMPILER_ID@" "${MBOOT_C_COMPILER_ID}" _consumer_cmakelists "${_consumer_cmakelists}")
file(WRITE "${_consumer_src}/CMakeLists.txt" "${_consumer_cmakelists}")

file(WRITE "${_consumer_src}/consumer.c" [[
#include "mboot.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    mboot_wire_t slots[2];
    bool present[2];
} backend_t;

static uint32_t clock_now(void)
{
    return 7u;
}

static mboot_slot_io_result_t read_slot(uint8_t slot, mboot_wire_t *out, void *ctx)
{
    backend_t *backend = (backend_t *)ctx;
    if (!backend->present[slot]) {
        return MBOOT_SLOT_IO_EMPTY;
    }
    *out = backend->slots[slot];
    return MBOOT_SLOT_IO_OK;
}

static mboot_slot_io_result_t write_slot(uint8_t slot, const mboot_wire_t *in, void *ctx)
{
    backend_t *backend = (backend_t *)ctx;
    backend->slots[slot] = *in;
    backend->present[slot] = true;
    return MBOOT_SLOT_IO_OK;
}

static mboot_err_t has_crash(bool *has_crash, void *ctx)
{
    (void)ctx;
    *has_crash = false;
    return MBOOT_OK;
}

static mboot_reason_t detect_reason(void *ctx)
{
    (void)ctx;
    return MBOOT_REASON_COLD;
}

int main(void)
{
    backend_t backend;
    mboot_io_t io;
    mboot_config_t config;
    mboot_t boot;

    memset(&backend, 0, sizeof(backend));
    config = mboot_default_config();
    io.read_slot = read_slot;
    io.write_slot = write_slot;
    io.has_crash = has_crash;
    io.detect_reason = detect_reason;
    io.io_ctx = &backend;

    if (mboot_init(&boot, &io, clock_now, &config) != MBOOT_OK) {
        return 1;
    }
    if (mboot_start(&boot) != MBOOT_OK) {
        return 2;
    }
    return mboot_mode(&boot) == MBOOT_MODE_NORMAL ? 0 : 3;
}
]])

set(_consumer_configure_args
    -G "${MBOOT_GENERATOR}"
    -DCMAKE_C_COMPILER=${MBOOT_C_COMPILER}
    -S "${_consumer_src}"
    -B "${_consumer_build}"
    -DCMAKE_PREFIX_PATH=${_prefix}
)
if(DEFINED MBOOT_C_COMPILER_TARGET AND NOT MBOOT_C_COMPILER_TARGET STREQUAL "")
    list(APPEND _consumer_configure_args -DCMAKE_C_COMPILER_TARGET=${MBOOT_C_COMPILER_TARGET})
endif()
if(DEFINED MBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN AND NOT MBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN STREQUAL "")
    list(APPEND _consumer_configure_args -DCMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN=${MBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN})
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_consumer_configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure failed\n${configure_stdout}\n${configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build}" --config "${MBOOT_CONFIG}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build failed\n${build_stdout}\n${build_stderr}")
endif()

if(WIN32)
    set(_consumer_name "consumer.exe")
else()
    set(_consumer_name "consumer")
endif()

set(_consumer_exe "${_consumer_build}/${MBOOT_CONFIG}/${_consumer_name}")
if(NOT EXISTS "${_consumer_exe}")
    set(_consumer_exe "${_consumer_build}/${_consumer_name}")
endif()

execute_process(
    COMMAND "${_consumer_exe}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed\n${run_stdout}\n${run_stderr}")
endif()
