cmake_minimum_required(VERSION 3.1)
project(popkcel)

cmake_policy(SET CMP0003 NEW)
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POPKCEL_SHARED "使用动态库" OFF)
option(POPKCEL_PSR "用UDP模拟TCP功能" ON)
option(POPKCEL_FAKESYNC "伪同步功能，用协程实现，可以像写同步操作一样写异步操作" ON)
option(POPKCEL_MULTITHREAD "多线程支持，windows下只支持单线程" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

if(SHARED)
    set(ELBT SHARED)
else(SHARED)
    if(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
        set(ELBT SHARED)
    else(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
        set(ELBT STATIC)
    endif(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
endif(SHARED)

#[[
if(${CMAKE_VERSION} VERSION_LESS "3.20.0")
    TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
    if(IS_BIG_ENDIAN)
        add_definitions(-DPOPKCEL_BIGENDIAN)
    endif()
else()
    if(CMAKE_C_BYTE_ORDER EQUAL BIG_ENDIAN)
        add_compile_definitions(POPKCEL_BIGENDIAN)
    endif()
endif()
]]

include(custom.cmake OPTIONAL)

add_subdirectory(popkcel)
#add_subdirectory(eldht)
add_subdirectory(test)

add_custom_target(doc doxygen ${CMAKE_SOURCE_DIR}/Doxyfile
WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
