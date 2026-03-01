# File Transfer Example
# Demonstrates reliable file transfer with fragmentation
# Author: Nexus Team

add_executable(file_transfer
    main.c
)

target_link_libraries(file_transfer
    PRIVATE xgl
)
