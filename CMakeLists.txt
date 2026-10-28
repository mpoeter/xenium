cmake_minimum_required(VERSION 3.14)

project(xenium
  VERSION 0.1.0
  DESCRIPTION "Header-only library providing concurrent data structures and memory reclamation algorithms with customizable reclamation schemes"
  HOMEPAGE_URL "https://github.com/mpoeter/xenium"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CheckCXXCompilerFlag)
include(CMakePushCheckState)

# Only default tests/benchmarks ON when xenium is the top-level project, so
# consumers pulling xenium in via FetchContent/add_subdirectory don't build
# our test suite by default.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(XENIUM_IS_TOP_LEVEL ON)
else()
  set(XENIUM_IS_TOP_LEVEL OFF)
endif()

option(XENIUM_BUILD_TESTS "Build tests" ${XENIUM_IS_TOP_LEVEL})
option(XENIUM_BUILD_BENCHMARKS "Build benchmarks" ${XENIUM_IS_TOP_LEVEL})
option(XENIUM_WITH_TSAN "Build tests and benchmarks with ThreadSanitizer" OFF)
option(XENIUM_BUILD_DOCUMENTATION "Create the HTML based documentation (requires Doxygen)" OFF)
option(XENIUM_BENCHMARK_WITH_LIBCDS "Build benchmarks with libcds comparison" OFF)

find_package(Threads REQUIRED)

add_library(xenium INTERFACE)
add_library(xenium::xenium ALIAS xenium)

target_include_directories(xenium INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(xenium INTERFACE cxx_std_17)
target_link_libraries(xenium INTERFACE Threads::Threads)

function(xenium_enable_tsan target)
  if(XENIUM_WITH_TSAN AND NOT MSVC)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_FLAGS -fsanitize=thread)
    check_cxx_compiler_flag("" XENIUM_TSAN_FLAG_WORKS)
    cmake_pop_check_state()

    if(XENIUM_TSAN_FLAG_WORKS)
      target_compile_options(${target} PRIVATE -fsanitize=thread)
      target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
  endif()
endfunction()

function(xenium_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /bigobj /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-error=cpp)
  endif()
endfunction()

if(XENIUM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(XENIUM_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(XENIUM_BUILD_DOCUMENTATION)
  find_package(Doxygen REQUIRED)
  add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
endif()

include(CMakePackageConfigHelpers)

install(TARGETS xenium EXPORT xenium-targets)
install(DIRECTORY include/ TYPE INCLUDE)

install(EXPORT xenium-targets
  FILE xenium-targets.cmake
  NAMESPACE xenium::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xenium)

configure_package_config_file(
  cmake/xenium-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/xenium-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xenium)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/xenium-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/xenium-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/xenium-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xenium)
