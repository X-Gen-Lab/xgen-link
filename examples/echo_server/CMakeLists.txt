# Echo Server Example
# Simple echo server demonstrating basic send/receive
# Author: Nexus Team

add_executable(echo_server
    main.c
)

target_link_libraries(echo_server
    PRIVATE xgl
)

if(XGL_BUILD_TESTS)
    add_test(NAME xgl_echo_example COMMAND echo_server)
    set_tests_properties(xgl_echo_example PROPERTIES TIMEOUT 10)
endif()
