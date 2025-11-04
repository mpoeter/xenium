cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

#
# Project
#
project(
  xenium
  VERSION 0.0.2	
  LANGUAGES CXX
  DESCRIPTION "Header-only library providing concurrent data structures and memory reclamation algorithms with customizable reclamation schemes" 
)

#
# Target.
#
set(TARGET_NAME ${PROJECT_NAME})

#
# Settings
#
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX d)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Options
#
include(CheckCXXCompilerFlag)
include(CMakePushCheckState)
include(CMakeDependentOption)

option(XENIUM_CACHE_DEPS "Locally cache dependencies" ON)
option(XENIUM_BUILD_DOCS "Build documentation" OFF)
option(XENIUM_BUILD_TESTS "Build tests" ON)
option(XENIUM_BENCHMARK "Build Benchmark" ON)
option(XENIUM_WITH_TSAN "Build tests and benchmarks with ThreadSanitizer" ON)
# option(XENIUM_BENCHMARK_WITH_LIBDCDS "Build benchmarks with libcds" OFF)

#
# Runtime library
#
if(XENIUM_MSVC_RUNTIME_STATIC AND MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

#
# Includes
#
include(GNUInstallDirs)
include(GenerateExportHeader)
include(CPM)
if(XENIUM_BUILD_DOCS)
  find_package(Doxygen)
endif()


find_package(Threads REQUIRED)

# Cache the downloaded packets to avoid re-downloading them
if(XENIUM_CACHE_DEPS)
  set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/cmake-build-deps/")
endif()

if(XENIUM_BENCHMARK)
# No need to download taocpp_json separately, because taocpp_config already exposes it.
# https://github.com/taocpp/json/

# https://github.com/taocpp/config
 CPMAddPackage(
   NAME taocpp_config
   GITHUB_REPOSITORY taocpp/config
   GIT_TAG 93a455e103cdd8030e770dfff4e2d64d9057ed46
   EXCLUDE_FROM_ALL ON
   SYSTEM ON
 )

  # if(XENIUM_BENCHMARK_WITH_LIBDCDS)
# 
  #  CPMAddPackage(
  #     NAME libcds
  #     GITHUB_REPOSITORY khizmax/libcds
  #     GIT_TAG v2.3.3
  #     EXCLUDE_FROM_ALL ON
  #     SYSTEM ON
  #   )
  # endif()
endif()

if(XENIUM_BUILD_TESTS)
 # https://github.com/google/googletest
  CPMAddPackage(
    NAME GoogleTest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.15.2
    EXCLUDE_FROM_ALL ON
    SYSTEM ON
    OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt ON"
  )
endif()

#
# Headers
#
set(XENIUM_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/${PROJECT_NAME}")
set(XENIUM_HEADERS 
# cmake-format: sortable
"${XENIUM_HEADERS_DIR}/acquire_guard.hpp"
"${XENIUM_HEADERS_DIR}/aligned_object.hpp"
"${XENIUM_HEADERS_DIR}/backoff.hpp"
"${XENIUM_HEADERS_DIR}/chase_work_stealing_deque.hpp"
"${XENIUM_HEADERS_DIR}/detail"
"${XENIUM_HEADERS_DIR}/harris_michael_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/harris_michael_list_based_set.hpp"
"${XENIUM_HEADERS_DIR}/hash.hpp"
"${XENIUM_HEADERS_DIR}/impl"
"${XENIUM_HEADERS_DIR}/kirsch_bounded_kfifo_queue.hpp"
"${XENIUM_HEADERS_DIR}/kirsch_kfifo_queue.hpp"
"${XENIUM_HEADERS_DIR}/left_right.hpp"
"${XENIUM_HEADERS_DIR}/marked_ptr.hpp"
"${XENIUM_HEADERS_DIR}/meta.hpp"
"${XENIUM_HEADERS_DIR}/michael_scott_queue.hpp"
"${XENIUM_HEADERS_DIR}/nikolaev_bounded_queue.hpp"
"${XENIUM_HEADERS_DIR}/nikolaev_queue.hpp"
"${XENIUM_HEADERS_DIR}/parameter.hpp"
"${XENIUM_HEADERS_DIR}/policy.hpp"
"${XENIUM_HEADERS_DIR}/ramalhete_queue.hpp"
"${XENIUM_HEADERS_DIR}/reclamation"
"${XENIUM_HEADERS_DIR}/seqlock.hpp"
"${XENIUM_HEADERS_DIR}/utils.hpp"
"${XENIUM_HEADERS_DIR}/vyukov_bounded_queue.hpp"
"${XENIUM_HEADERS_DIR}/vyukov_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/acquire_guard.hpp"
"${XENIUM_HEADERS_DIR}/aligned_object.hpp"
"${XENIUM_HEADERS_DIR}/backoff.hpp"
"${XENIUM_HEADERS_DIR}/chase_work_stealing_deque.hpp"
"${XENIUM_HEADERS_DIR}/harris_michael_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/harris_michael_list_based_set.hpp"
"${XENIUM_HEADERS_DIR}/hash.hpp"
"${XENIUM_HEADERS_DIR}/kirsch_bounded_kfifo_queue.hpp"
"${XENIUM_HEADERS_DIR}/kirsch_kfifo_queue.hpp"
"${XENIUM_HEADERS_DIR}/left_right.hpp"
"${XENIUM_HEADERS_DIR}/marked_ptr.hpp"
"${XENIUM_HEADERS_DIR}/meta.hpp"
"${XENIUM_HEADERS_DIR}/michael_scott_queue.hpp"
"${XENIUM_HEADERS_DIR}/nikolaev_bounded_queue.hpp"
"${XENIUM_HEADERS_DIR}/nikolaev_queue.hpp"
"${XENIUM_HEADERS_DIR}/parameter.hpp"
"${XENIUM_HEADERS_DIR}/policy.hpp"
"${XENIUM_HEADERS_DIR}/ramalhete_queue.hpp"
"${XENIUM_HEADERS_DIR}/seqlock.hpp"
"${XENIUM_HEADERS_DIR}/utils.hpp"
"${XENIUM_HEADERS_DIR}/vyukov_bounded_queue.hpp"
"${XENIUM_HEADERS_DIR}/vyukov_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/detail/fixed_size_circular_array.hpp"
"${XENIUM_HEADERS_DIR}/detail/growing_circular_array.hpp"
"${XENIUM_HEADERS_DIR}/detail/hardware.hpp"
"${XENIUM_HEADERS_DIR}/detail/nikolaev_scq.hpp"
"${XENIUM_HEADERS_DIR}/detail/pointer_queue_traits.hpp"
"${XENIUM_HEADERS_DIR}/detail/port.hpp"
"${XENIUM_HEADERS_DIR}/detail/fixed_size_circular_array.hpp"
"${XENIUM_HEADERS_DIR}/detail/growing_circular_array.hpp"
"${XENIUM_HEADERS_DIR}/detail/hardware.hpp"
"${XENIUM_HEADERS_DIR}/detail/nikolaev_scq.hpp"
"${XENIUM_HEADERS_DIR}/detail/pointer_queue_traits.hpp"
"${XENIUM_HEADERS_DIR}/detail/port.hpp"
"${XENIUM_HEADERS_DIR}/impl/vyukov_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/impl/vyukov_hash_map_traits.hpp"
"${XENIUM_HEADERS_DIR}/impl/vyukov_hash_map.hpp"
"${XENIUM_HEADERS_DIR}/impl/vyukov_hash_map_traits.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail"
"${XENIUM_HEADERS_DIR}/reclamation/generic_epoch_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/hazard_eras.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/hazard_pointer.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl"
"${XENIUM_HEADERS_DIR}/reclamation/lock_free_ref_count.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/quiescent_state_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/stamp_it.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/generic_epoch_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/hazard_eras.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/hazard_pointer.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/lock_free_ref_count.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/quiescent_state_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/stamp_it.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/allocation_tracker.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/concurrent_ptr.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/deletable_object.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/guard_ptr.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/orphan.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/perf_counter.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/retire_list.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/thread_block_list.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/allocation_tracker.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/concurrent_ptr.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/deletable_object.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/guard_ptr.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/orphan.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/perf_counter.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/retire_list.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/detail/thread_block_list.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/generic_epoch_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/hazard_eras.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/hazard_pointer.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/lock_free_ref_count.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/quiescent_state_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/stamp_it.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/generic_epoch_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/hazard_eras.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/hazard_pointer.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/lock_free_ref_count.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/quiescent_state_based.hpp"
"${XENIUM_HEADERS_DIR}/reclamation/impl/stamp_it.hpp"
)

set(XENIUM_LIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(XENIUM_INCLUDE_DIRS_PUB "${XENIUM_LIB_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include")

if(XENIUM_BUILD_DOCS)
	if(NOT DOXYGEN_FOUND)
		message(FATAL_ERROR "Doxygen is needed to build the documentation.")
	endif()

	add_custom_target(doc
		COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/doc"
		COMMENT "Generating API documentation with Doxygen"
		VERBATIM)
endif()

add_library(${TARGET_NAME} INTERFACE ${XENIUM_HEADERS})
add_library(${TARGET_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})
# expose include dir
target_include_directories(${TARGET_NAME} INTERFACE ${XENIUM_INCLUDE_DIRS_PUB})

# test
if(XENIUM_BUILD_TESTS)
  include(GoogleTest)
  set(TEST_NAME ${TARGET_NAME}_test)
  
  set(XENIUM_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test")
  
  set(XENIUM_TESTS_HEADERS 
    "${XENIUM_TESTS_DIR}/helpers.hpp"
  )

  set(XENIUM_TESTS_SRC
    "${XENIUM_TESTS_DIR}/chase_work_stealing_deque_test.cpp"
    "${XENIUM_TESTS_DIR}/concurrent_ptr_test.cpp"
    "${XENIUM_TESTS_DIR}/harris_michael_hash_map_test.cpp"
    "${XENIUM_TESTS_DIR}/harris_michael_list_based_set_test.cpp"
    "${XENIUM_TESTS_DIR}/kirsch_bounded_kfifo_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/kirsch_kfifo_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/left_right_test.cpp"
    "${XENIUM_TESTS_DIR}/main.cpp"
    "${XENIUM_TESTS_DIR}/marked_ptr_test.cpp"
    "${XENIUM_TESTS_DIR}/michael_scott_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/nikolaev_bounded_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/nikolaev_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/parameter_test.cpp"
    "${XENIUM_TESTS_DIR}/ramalhete_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/sanitize_test.cpp"
    "${XENIUM_TESTS_DIR}/seqlock_test.cpp"
    "${XENIUM_TESTS_DIR}/vyukov_bounded_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/vyukov_hash_map_test.cpp"
    "${XENIUM_TESTS_DIR}/chase_work_stealing_deque_test.cpp"
    "${XENIUM_TESTS_DIR}/concurrent_ptr_test.cpp"
    "${XENIUM_TESTS_DIR}/harris_michael_hash_map_test.cpp"
    "${XENIUM_TESTS_DIR}/harris_michael_list_based_set_test.cpp"
    "${XENIUM_TESTS_DIR}/kirsch_bounded_kfifo_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/kirsch_kfifo_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/left_right_test.cpp"
    "${XENIUM_TESTS_DIR}/main.cpp"
    "${XENIUM_TESTS_DIR}/marked_ptr_test.cpp"
    "${XENIUM_TESTS_DIR}/michael_scott_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/nikolaev_bounded_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/nikolaev_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/parameter_test.cpp"
    "${XENIUM_TESTS_DIR}/ramalhete_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/sanitize_test.cpp"
    "${XENIUM_TESTS_DIR}/seqlock_test.cpp"
    "${XENIUM_TESTS_DIR}/vyukov_bounded_queue_test.cpp"
    "${XENIUM_TESTS_DIR}/vyukov_hash_map_test.cpp"
    "${XENIUM_TESTS_DIR}/detail/nikolaev_scq_test.cpp"
    "${XENIUM_TESTS_DIR}/detail/nikolaev_scq_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/generic_epoch_based_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/hazard_eras_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/hazard_pointer_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/lock_free_ref_count_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/quiescent_state_based_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/stamp_it_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/generic_epoch_based_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/hazard_eras_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/hazard_pointer_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/lock_free_ref_count_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/quiescent_state_based_test.cpp"
    "${XENIUM_TESTS_DIR}/reclamation/stamp_it_test.cpp"
  )
  add_executable(${TEST_NAME} ${XENIUM_TESTS_HEADERS} ${XENIUM_TESTS_SRC})
  target_link_libraries(
    ${TEST_NAME}
    PRIVATE
    GTest::gtest_main
    ${TARGET_NAME}
  )
  
  #
  # Target compile option
  #
  if(MSVC)
    target_compile_options(${TEST_NAME} PRIVATE /bigobj /W4)
  else()
   target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror -Wno-error=cpp)
  endif()
  
endif()

if(XENIUM_BENCHMARK)
  
  set(XENIUM_BENCHMARK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
  set(XENIUM_BENCHMARK_HEADERS
  "${XENIUM_BENCHMARK_DIR}/benchmark.hpp"
  "${XENIUM_BENCHMARK_DIR}/config.hpp"
  "${XENIUM_BENCHMARK_DIR}/descriptor.hpp"
  "${XENIUM_BENCHMARK_DIR}/execution.hpp"
  "${XENIUM_BENCHMARK_DIR}/hash_maps.hpp"
  "${XENIUM_BENCHMARK_DIR}/queues.hpp"
  "${XENIUM_BENCHMARK_DIR}/reclaimers.hpp"
  "${XENIUM_BENCHMARK_DIR}/report.hpp"
  "${XENIUM_BENCHMARK_DIR}/workload.hpp"
  )
  set(XENIUM_BENCHMARK_SRC
  "${XENIUM_BENCHMARK_DIR}/benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/execution.cpp"
  "${XENIUM_BENCHMARK_DIR}/hash_map_benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/main.cpp"
  "${XENIUM_BENCHMARK_DIR}/queue_benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/report.cpp"
  "${XENIUM_BENCHMARK_DIR}/workload.cpp"
  "${XENIUM_BENCHMARK_DIR}/benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/execution.cpp"
  "${XENIUM_BENCHMARK_DIR}/hash_map_benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/main.cpp"
  "${XENIUM_BENCHMARK_DIR}/queue_benchmark.cpp"
  "${XENIUM_BENCHMARK_DIR}/report.cpp"
  "${XENIUM_BENCHMARK_DIR}/workload.cpp"
  )

  add_executable(benchmark ${XENIUM_BENCHMARK_HEADERS} ${XENIUM_BENCHMARK_SRC})

  target_link_libraries(
    benchmark
    PRIVATE
    ${TARGET_NAME}
    taocpp-json
    taocpp-config
    # $<$<BOOL:${XENIUM_BENCHMARK_WITH_LIBDCDS}>:cds>
  )

  if(MSVC)
    target_compile_options(benchmark PRIVATE /bigobj /W4 )
  else()
    target_compile_options(benchmark PRIVATE /bigobj /W4)
  endif()
   
  # else()
  #  if(XENIUM_BENCHMARK_WITH_LIBDCDS)
  #     target_compile_options(benchmark PRIVATE -Wall -Wextra -Werror -Wno-error=cpp WITH_LIBCDS)
  #   else()
  #     target_compile_options(benchmark PRIVATE -Wall -Wextra -Werror -Wno-error=cpp)
  #   endif()
endif()

if(XENIUM_WITH_TSAN AND NOT MSVC)
	cmake_push_check_state()
	set(CMAKE_REQUIRED_FLAGS -fsanitize=thread)
	check_cxx_compiler_flag("" TSAN_FLAG_WORKS)
	cmake_pop_check_state()

	if(TSAN_FLAG_WORKS)
		string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread")
	endif()
endif()