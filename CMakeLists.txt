project(xenium)
cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(Boost 1.61.0 REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE XENIUM_FILES xenium/*.hpp)

file(GLOB_RECURSE TEST_FILES test/*.cpp)

include_directories(. ${Boost_INCLUDE_DIR} ${GTEST_INCLUDE_DIRS})

add_executable(gtest ${TEST_FILES} ${XENIUM_FILES})

target_link_libraries(gtest ${GTEST_BOTH_LIBRARIES})

if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(gtest "${CMAKE_THREAD_LIBS_INIT}")
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	add_definitions(-DDEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebug)
	add_definitions(-DNDEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES Release)
	add_definitions(-DNDEBUG)
endif()

add_test(AllTests gtest)