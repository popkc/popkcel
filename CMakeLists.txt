cmake_minimum_required(VERSION 3.16...3.30)
project(popkcel)

option(POPKCEL_SHARED "编译为动态库" ON)
option(POPKCEL_FAKESYNC "伪同步功能，用协程实现，可以像写同步操作一样写异步操作" ON)
option(POPKCEL_MULTITHREAD "多线程支持，windows下只支持单线程" ON)

if(POPKCEL_SHARED)
    set(ELBT SHARED)
else()
    if(UNIX AND CMAKE_BUILD_TYPE STREQUAL Debug)
        set(ELBT SHARED)
    else()
        set(ELBT STATIC)
    endif()
endif()

include(custom.cmake OPTIONAL)

add_subdirectory(popkcel)
#add_subdirectory(eldht)
add_subdirectory(test)

add_custom_target(doc doxygen ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
