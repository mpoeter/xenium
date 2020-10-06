project(xenium)
cmake_minimum_required(VERSION 3.0)

include(CheckCXXCompilerFlag)
include(CMakePushCheckState)

include(3rdParty/gtest.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(Threads REQUIRED)
find_package(Doxygen)

option(WITH_TSAN "Build tests and benchmarks with ThreadSanitizer" ON)
option(BUILD_DOCUMENTATION "Create the HTML based documentation (requires Doxygen)" ${DOXYGEN_FOUND})

file(GLOB_RECURSE XENIUM_FILES xenium/*.hpp)

file(GLOB_RECURSE TEST_FILES test/*.cpp)

file(GLOB_RECURSE BENCHMARK_FILES benchmarks/*.cpp)

include_directories(.)

add_executable(gtest ${TEST_FILES} ${XENIUM_FILES})
target_link_libraries(gtest googletest)

add_executable(benchmark ${BENCHMARK_FILES} ${XENIUM_FILES})
target_include_directories(
	benchmark
	SYSTEM
	PRIVATE
	./3rdParty/json/include
	./3rdParty/config/include
)

if(CMAKE_THREAD_LIBS_INIT)
	target_link_libraries(gtest "${CMAKE_THREAD_LIBS_INIT}")
	target_link_libraries(benchmark "${CMAKE_THREAD_LIBS_INIT}")
endif()

if(WITH_LIBCDS)
	find_package(LibCDS CONFIG REQUIRED)
	target_link_libraries(benchmark LibCDS::cds)
	target_compile_definitions(benchmark PRIVATE WITH_LIBCDS CDS_THREADING_CXX11)
endif()

if(WITH_TSAN AND NOT MSVC)
	cmake_push_check_state()
	set(CMAKE_REQUIRED_FLAGS -fsanitize=thread)
	check_cxx_compiler_flag("" TSAN_FLAG_WORKS)
	cmake_pop_check_state()
	if(TSAN_FLAG_WORKS)
		string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread")
	endif()
endif()

if(MSVC)
	target_compile_options(gtest PRIVATE /bigobj /W4)# /WX)
	target_compile_options(benchmark PRIVATE /bigobj)# /W4 /WX)
else()
	target_compile_options(gtest PRIVATE -Wall -Wextra -Werror -Wno-error=cpp)
	target_compile_options(benchmark PRIVATE -Wall -Wextra -Wno-error=cpp)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	add_definitions(-DDEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebug)
	add_definitions(-DNDEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES Release)
	add_definitions(-DNDEBUG)
endif()

if(BUILD_DOCUMENTATION)
	if(NOT DOXYGEN_FOUND)
		message(FATAL_ERROR "Doxygen is needed to build the documentation.")
	endif()

	add_custom_target(doc
			COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc
			COMMENT "Generating API documentation with Doxygen"
			VERBATIM)
endif()

add_test(AllTests gtest)
