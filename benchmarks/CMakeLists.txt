# taocpp/json and taocpp/config are version-coupled (config's headers assume
# json's old bundled PEGTL layout) and config's current API is incompatible
# with this code (e.g. it dropped tao/config/schema.hpp), so both stay on
# these older commits rather than tracking upstream.
include(FetchContent)

# Consumed as headers only: their own CMakeLists.txt build logic (tests,
# install rules) is never invoked, and the include dirs are marked SYSTEM so
# warnings inside their headers aren't promoted to errors by our -Werror.
FetchContent_Declare(taocpp_json
  GIT_REPOSITORY https://github.com/taocpp/json.git
  GIT_TAG 879f27eaf8b2d20f9187ca823256dc1da254fad5)
FetchContent_GetProperties(taocpp_json)
if(NOT taocpp_json_POPULATED)
  # Unlike config below, taocpp/json ships its own CMakeLists.txt, so
  # Populate (not MakeAvailable) is used to avoid processing it.
  if(POLICY CMP0169) # silence deprecation warning for this form of Populate()
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
  endif()
  FetchContent_Populate(taocpp_json)
  if(POLICY CMP0169)
    cmake_policy(POP)
  endif()
endif()

add_library(taocpp_json_headers INTERFACE)
add_library(taocpp::json ALIAS taocpp_json_headers)
target_include_directories(taocpp_json_headers SYSTEM INTERFACE "${taocpp_json_SOURCE_DIR}/include")

FetchContent_Declare(taocpp_config
  GIT_REPOSITORY https://github.com/taocpp/config.git
  GIT_TAG 46e0ab2b6bc8228276d154cd339f9d8b8772d707)
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
