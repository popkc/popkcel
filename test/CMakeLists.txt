
aux_source_directory(. TESTSRC)

add_executable(eltest ${TESTSRC})
target_link_libraries(eltest popkcel)
target_include_directories(eltest PUBLIC "${PROJECT_SOURCE_DIR}/popkcel")