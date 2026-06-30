cmake_minimum_required(VERSION 3.20)

project(microboot LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 99 CACHE STRING "C standard for microboot")
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11 CACHE STRING "C++ standard for microboot tests")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MICROBOOT_BUILD_TESTS "Build microboot tests and fixtures" ON)

add_library(microboot src/mboot.c)
add_library(microboot::microboot ALIAS microboot)

target_include_directories(microboot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(microboot PUBLIC c_std_99)

if(MSVC)
    target_compile_options(microboot PRIVATE /W4 /WX /permissive-)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(microboot PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-command-line-argument)
else()
    target_compile_options(microboot PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS microboot
    EXPORT microbootTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/mboot.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT microbootTargets
    FILE microbootTargets.cmake
    NAMESPACE microboot::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microboot
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/microbootConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/microbootConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microboot
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/microbootConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microboot
)

if(BUILD_TESTING AND MICROBOOT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
