# Multi-Node Network Example
# Three-node network demonstrating routing
# Author: Nexus Team

add_executable(multi_node
    main.c
)

target_link_libraries(multi_node
    PRIVATE xgl
)
