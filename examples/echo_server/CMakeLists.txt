# Echo Server Example
# Simple echo server demonstrating basic send/receive
# Author: Nexus Team

add_executable(echo_server
    main.c
)

target_link_libraries(echo_server
    PRIVATE xgl
)
