add_executable(boot_update main.c)
target_link_libraries(boot_update PRIVATE xgl::xgl)
target_include_directories(boot_update PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(XGL_BUILD_TESTS OR XGL_BUILD_NOHEAP_SMOKE)
    add_test(NAME xgl_boot_update_example COMMAND boot_update)
    set_tests_properties(
        xgl_boot_update_example
        PROPERTIES LABELS integration TIMEOUT 10
    )
endif()
