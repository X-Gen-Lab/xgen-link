# Echo Server Example
# Simple echo server demonstrating basic send/receive
# Author: X-Gen Lab

add_executable(echo_server
    main.c
)

target_link_libraries(echo_server
    PRIVATE xgl
)

if(XGL_BUILD_TESTS OR XGL_BUILD_NOHEAP_SMOKE)
    add_test(NAME xgl_echo_example COMMAND echo_server)
    set_tests_properties(xgl_echo_example PROPERTIES LABELS integration TIMEOUT 10)
endif()
