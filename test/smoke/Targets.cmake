set(noheap_test_library xgl)
if(XGL_ALLOW_FALLBACK_MALLOC)
    xgl_add_library(xgl_noheap OFF "${CMAKE_CURRENT_BINARY_DIR}/generated-noheap")
    set(noheap_test_library xgl_noheap)
endif()

add_executable(
    xgl_static_workspace_smoke
    "${CMAKE_CURRENT_LIST_DIR}/static_workspace_smoke.c"
)
target_link_libraries(xgl_static_workspace_smoke PRIVATE ${noheap_test_library})
add_test(NAME xgl_static_workspace_smoke COMMAND xgl_static_workspace_smoke)
set_tests_properties(
    xgl_static_workspace_smoke
    PROPERTIES LABELS integration TIMEOUT 30
)

if(NOT XGL_PROFILE STREQUAL "boot")
    add_executable(xgl_noheap_smoke "${CMAKE_CURRENT_LIST_DIR}/noheap_smoke.c")
    target_link_libraries(xgl_noheap_smoke PRIVATE ${noheap_test_library})
    target_include_directories(
        xgl_noheap_smoke
        PRIVATE "${PROJECT_SOURCE_DIR}/src"
    )
    add_test(NAME xgl_noheap_smoke COMMAND xgl_noheap_smoke)
    set_tests_properties(
        xgl_noheap_smoke
        PROPERTIES LABELS integration TIMEOUT 30
    )
endif()
