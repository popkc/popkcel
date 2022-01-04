cmake_minimum_required(VERSION 3.1)
project(popkcel)

cmake_policy(SET CMP0003 NEW)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
    set(ELBT SHARED)
else(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
    set(ELBT STATIC)
endif(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)

include(custom.txt OPTIONAL)

add_subdirectory(popkcel)
#add_subdirectory(eldht)
add_subdirectory(test)

#add_dependencies(eldht popkcel)
add_dependencies(eltest popkcel)

add_custom_target(doc doxygen ${CMAKE_SOURCE_DIR}/Doxyfile
WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
