
aux_source_directory(. TESTSRC)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(ELTESTTYPE )
else(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(ELTESTTYPE EXCLUDE_FROM_ALL)
endif(CMAKE_BUILD_TYPE STREQUAL Debug)

add_executable(eltest ${ELTESTTYPE} ${TESTSRC})
target_link_libraries(eltest popkcel)
target_include_directories(eltest PUBLIC "${PROJECT_SOURCE_DIR}/popkcel")
