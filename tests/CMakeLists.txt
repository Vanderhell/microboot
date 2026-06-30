function(mboot_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-command-line-argument)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_executable(mboot_runtime_test test_all.c)
target_link_libraries(mboot_runtime_test PRIVATE microboot)
mboot_enable_strict_warnings(mboot_runtime_test)
add_test(NAME runtime COMMAND mboot_runtime_test)

add_executable(mboot_readme_example readme_example.c)
target_link_libraries(mboot_readme_example PRIVATE microboot)
mboot_enable_strict_warnings(mboot_readme_example)
add_test(NAME readme_example COMMAND mboot_readme_example)

add_executable(mboot_header_twice header_twice.c)
target_link_libraries(mboot_header_twice PRIVATE microboot)
mboot_enable_strict_warnings(mboot_header_twice)
add_test(NAME header_twice COMMAND mboot_header_twice)

add_executable(mboot_multi_tu multi_tu_main.c multi_tu_helper.c)
target_link_libraries(mboot_multi_tu PRIVATE microboot)
mboot_enable_strict_warnings(mboot_multi_tu)
add_test(NAME multi_tu COMMAND mboot_multi_tu)

add_executable(mboot_cpp_consumer cpp_consumer.cpp)
target_link_libraries(mboot_cpp_consumer PRIVATE microboot)
target_compile_features(mboot_cpp_consumer PRIVATE cxx_std_11)
mboot_enable_strict_warnings(mboot_cpp_consumer)
add_test(NAME cpp_consumer COMMAND mboot_cpp_consumer)

add_test(NAME compile_fail
    COMMAND ${CMAKE_COMMAND}
        -DMBOOT_GENERATOR=${CMAKE_GENERATOR}
        -DMBOOT_C_COMPILER=${CMAKE_C_COMPILER}
        -DMBOOT_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}
        -DMBOOT_C_COMPILER_TARGET=${CMAKE_C_COMPILER_TARGET}
        -DMBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN=${CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN}
        -DMBOOT_C_FLAGS=${CMAKE_C_FLAGS}
        -DMBOOT_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DMBOOT_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
        -DMBOOT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DMBOOT_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DMBOOT_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_fail.cmake
)

add_test(NAME install_consumer
    COMMAND ${CMAKE_COMMAND}
        -DMBOOT_GENERATOR=${CMAKE_GENERATOR}
        -DMBOOT_C_COMPILER=${CMAKE_C_COMPILER}
        -DMBOOT_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}
        -DMBOOT_C_COMPILER_TARGET=${CMAKE_C_COMPILER_TARGET}
        -DMBOOT_C_COMPILER_EXTERNAL_TOOLCHAIN=${CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN}
        -DMBOOT_C_FLAGS=${CMAKE_C_FLAGS}
        -DMBOOT_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DMBOOT_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
        -DMBOOT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DMBOOT_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DMBOOT_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install_consumer.cmake
)
