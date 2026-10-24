include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.18.0) # latest release; the previously-vendored submodule commit predates CMake's removal of <3.5 policy support and no longer configures
FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE XENIUM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(xenium_test ${XENIUM_TEST_SOURCES})
target_link_libraries(xenium_test PRIVATE xenium::xenium GTest::gtest_main)

xenium_enable_warnings(xenium_test)
xenium_enable_tsan(xenium_test)

include(GoogleTest)
# PRE_TEST defers running the test binary (to enumerate individual tests)
# from build time to ctest time. Needed because TSan-instrumented binaries
# can fail under restrictive ASLR settings (see the "vm.mmap_rnd_bits"
# workaround in .circleci/config.yml) - discovering at build time would run
# the binary before that workaround has a chance to apply.
gtest_discover_tests(xenium_test DISCOVERY_MODE PRE_TEST)
