# taocpp/json and taocpp/config are pinned to the exact commits previously
# vendored as submodules (2020), NOT updated to current releases/HEAD: the
# two libraries are version-coupled (config's old include tree expects
# json's old bundled-PEGTL header layout, which json's current versions no
# longer have), and taocpp/config's current main has restructured its schema
# API (include/tao/config/schema.hpp no longer exists) - benchmarks/main.cpp
# uses that header plus tao::config::internal::configurator directly, so
# updating config requires porting against its current (undocumented,
# never-tagged-stable, still-"WiP") internal API, not just a version bump.
# googletest (below, in test/CMakeLists.txt) has no such coupling and is
# updated to its latest release.
include(FetchContent)

# taocpp/json ships a real CMakeLists.txt exporting taocpp::json, but builds
# its own tests/examples by default (and calls enable_testing()), and
# unconditionally installs itself (no option to disable it). Disable the
# tests/examples via cache vars, and use add_subdirectory(EXCLUDE_FROM_ALL)
# directly (rather than FetchContent_MakeAvailable) so its install() rules
# don't leak into `cmake --install` for this project.
set(TAOCPP_JSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TAOCPP_JSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(taocpp_json
  GIT_REPOSITORY https://github.com/taocpp/json.git
  GIT_TAG 879f27eaf8b2d20f9187ca823256dc1da254fad5) # pinned to the commit previously vendored as a submodule
FetchContent_GetProperties(taocpp_json)
if(NOT taocpp_json_POPULATED)
  # CMP0169 OLD: the single-arg FetchContent_Populate() form is deprecated in
  # favor of EXCLUDE_FROM_ALL on FetchContent_Declare() (CMake 3.28+), but
  # that isn't available at this project's 3.14 floor.
  if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
  endif()
  FetchContent_Populate(taocpp_json)
  if(POLICY CMP0169)
    cmake_policy(POP)
  endif()
  add_subdirectory(${taocpp_json_SOURCE_DIR} ${taocpp_json_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# At this pinned commit, taocpp/config has no CMakeLists.txt of its own
# (only a Makefile) - populate the source without processing a build system
# and declare the include-only target manually. (Its current main branch has
# since gained a CMakeLists.txt that vendors its own copies of taocpp/json
# and PEGTL as git submodules, which is one more reason not to follow it -
# see the version-coupling note above.)
FetchContent_Declare(taocpp_config
  GIT_REPOSITORY https://github.com/taocpp/config.git
  GIT_TAG 46e0ab2b6bc8228276d154cd339f9d8b8772d707) # pinned to the commit previously vendored as a submodule
FetchContent_MakeAvailable(taocpp_config)

add_library(taocpp_config_headers INTERFACE)
add_library(taocpp::config ALIAS taocpp_config_headers)
target_include_directories(taocpp_config_headers SYSTEM INTERFACE "${taocpp_config_SOURCE_DIR}/include")

file(GLOB_RECURSE XENIUM_BENCHMARK_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(xenium_benchmark ${XENIUM_BENCHMARK_SOURCES})
target_link_libraries(xenium_benchmark PRIVATE xenium::xenium taocpp::json taocpp::config)

if(XENIUM_BENCHMARK_WITH_LIBCDS)
  find_package(LibCDS CONFIG REQUIRED)
  target_link_libraries(xenium_benchmark PRIVATE LibCDS::cds)
  target_compile_definitions(xenium_benchmark PRIVATE WITH_LIBCDS CDS_THREADING_CXX11)
endif()

xenium_enable_warnings(xenium_benchmark)
xenium_enable_tsan(xenium_benchmark)
