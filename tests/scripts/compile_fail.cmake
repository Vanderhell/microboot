if(NOT DEFINED MBOOT_SOURCE_DIR OR NOT DEFINED MBOOT_BINARY_DIR OR NOT DEFINED MBOOT_GENERATOR OR NOT DEFINED MBOOT_C_COMPILER OR NOT DEFINED MBOOT_C_COMPILER_ID)
    message(FATAL_ERROR "MBOOT_SOURCE_DIR, MBOOT_BINARY_DIR, MBOOT_GENERATOR, MBOOT_C_COMPILER, and MBOOT_C_COMPILER_ID are required")
endif()

set(_root "${MBOOT_BINARY_DIR}/compile-fail")
file(MAKE_DIRECTORY "${_root}")

function(mboot_expect_compile_failure name source_text)
    set(expected_tokens ${ARGN})
    set(case_dir "${_root}/${name}")
    file(REMOVE_RECURSE "${case_dir}")
    file(MAKE_DIRECTORY "${case_dir}")
    file(WRITE "${case_dir}/bad.c" "${source_text}")
    file(WRITE "${case_dir}/CMakeLists.txt" [[
cmake_minimum_required(VERSION 3.20)
project(bad_case C)
add_executable(bad bad.c)
target_include_directories(bad PRIVATE "@MBOOT_SOURCE_DIR@/include")
if(MSVC)
    target_compile_options(bad PRIVATE /W4 /WX /permissive-)
elseif("@MBOOT_C_COMPILER_ID@" STREQUAL "Clang")
    target_compile_options(bad PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-command-line-argument)
else()
    target_compile_options(bad PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
]])
    file(READ "${case_dir}/CMakeLists.txt" _cmakelists)
    string(REPLACE "@MBOOT_SOURCE_DIR@" "${MBOOT_SOURCE_DIR}" _cmakelists "${_cmakelists}")
    string(REPLACE "@MBOOT_C_COMPILER_ID@" "${MBOOT_C_COMPILER_ID}" _cmakelists "${_cmakelists}")
    file(WRITE "${case_dir}/CMakeLists.txt" "${_cmakelists}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -G "${MBOOT_GENERATOR}" -DCMAKE_C_COMPILER=${MBOOT_C_COMPILER} -DCMAKE_C_COMPILER_TARGET=${MBOOT_C_COMPILER_TARGET} -DCMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN=${MBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN} -S "${case_dir}" -B "${case_dir}/build"
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_stdout
        ERROR_VARIABLE config_stderr
    )
    if(NOT config_result EQUAL 0)
        message(FATAL_ERROR "configure failed unexpectedly for ${name}\n${config_stdout}\n${config_stderr}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${case_dir}/build"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
    )
    set(build_output "${build_stdout}\n${build_stderr}")
    if(build_result EQUAL 0)
        message(FATAL_ERROR "compile-fail case unexpectedly built: ${name}\n${build_output}")
    endif()
    if(build_output MATCHES "^[ \t]*$")
        message(FATAL_ERROR "compile-fail case produced no diagnostic output: ${name}")
    endif()
    string(FIND "${build_output}" "bad.c" bad_c_pos)
    if(bad_c_pos EQUAL -1)
        message(FATAL_ERROR "compile-fail case did not report bad.c for ${name}\n${build_output}")
    endif()

    set(token_found FALSE)
    foreach(token IN LISTS expected_tokens)
        string(FIND "${build_output}" "${token}" token_pos)
        if(NOT token_pos EQUAL -1)
            set(token_found TRUE)
            break()
        endif()
    endforeach()
    if(NOT token_found)
        message(FATAL_ERROR "compile-fail case did not report any expected token for ${name}\n${build_output}")
    endif()
endfunction()

mboot_expect_compile_failure("bad_read_slot" [[
#include "mboot.h"
static mboot_slot_io_result_t bad_read(mboot_wire_t *out, void *ctx)
{
    (void)out;
    (void)ctx;
    return MBOOT_SLOT_IO_OK;
}
int main(void)
{
    mboot_io_t io = { bad_read, 0, 0, 0, 0 };
    (void)io;
    return 0;
}
]] mboot_read_slot_fn)

mboot_expect_compile_failure("bad_write_slot" [[
#include "mboot.h"
static mboot_slot_io_result_t bad_write(uint8_t slot, mboot_wire_t *record, void *ctx)
{
    (void)slot;
    (void)record;
    (void)ctx;
    return MBOOT_SLOT_IO_OK;
}
int main(void)
{
    mboot_io_t io = { 0, bad_write, 0, 0, 0 };
    (void)io;
    return 0;
}
]] mboot_write_slot_fn)

mboot_expect_compile_failure("bad_clock" [[
#include "mboot.h"
static uint64_t bad_clock(void)
{
    return 0u;
}
int main(void)
{
    mboot_clock_fn clock = bad_clock;
    (void)clock;
    return 0;
}
]] mboot_clock_fn bad_clock)

mboot_expect_compile_failure("bad_reason" [[
#include "mboot.h"
static int bad_reason(void *ctx)
{
    (void)ctx;
    return 0;
}
int main(void)
{
    mboot_detect_reason_fn fn = bad_reason;
    (void)fn;
    return 0;
}
]] mboot_detect_reason_fn bad_reason)

mboot_expect_compile_failure("bad_decide" [[
#include "mboot.h"
static int bad_decide(const mboot_info_t *info, void *ctx)
{
    (void)info;
    (void)ctx;
    return 0;
}
int main(void)
{
    mboot_decide_fn fn = bad_decide;
    (void)fn;
    return 0;
}
]] mboot_decide_fn bad_decide)

mboot_expect_compile_failure("const_incorrect_write" [[
#include "mboot.h"
static mboot_slot_io_result_t bad_write(uint8_t slot, mboot_wire_t *record, void *ctx)
{
    (void)slot;
    (void)record;
    (void)ctx;
    return MBOOT_SLOT_IO_OK;
}
int main(void)
{
    mboot_write_slot_fn fn = bad_write;
    (void)fn;
    return 0;
}
]] mboot_write_slot_fn bad_write)

mboot_expect_compile_failure("old_raw_struct_api" [[
#include "mboot.h"
int main(void)
{
    mboot_read_fn read_fn = 0;
    mboot_write_fn write_fn = 0;
    mboot_record_t record;
    (void)read_fn;
    (void)write_fn;
    (void)record;
    return 0;
}
]] mboot_read_fn mboot_write_fn mboot_record_t)
