

#include(ExternalProject)

#ExternalProject_Add(
#    libcds
#    GIT_REPOSITORY https://github.com/khizmax/libcds.git
#    GIT_TAG v2.3.3

#    UPDATE_COMMAND ""
#    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=cds
#)

#add_dependencies(benchmark libcds)

#add_library(cds STATIC IMPORTED)

#target_link_libraries(benchmark cds)

#ExternalProject_Get_Property(libcds install_dir)
#message(${install_dir})

#set(cds_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/3rdparty/libcds/")
#set(cds_LIBRARIES "${CMAKE_SHARED_LIBRARY_PREFIX}cds${CMAKE_SHARED_LIBRARY_SUFFIX}")

#TARGET_LINK_LIBRARIES(benchmark ${CDS_SHARED_LIBRARY} )
#target_include_directories(benchmark PRIVATE ${cds_INCLUDE_DIRS})
