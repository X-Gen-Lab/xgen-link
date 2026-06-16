# File Transfer Example
# Demonstrates reliable file transfer with fragmentation
# Author: X-Gen Lab

add_executable(file_transfer
    main.c
)

target_link_libraries(file_transfer
    PRIVATE xgl
)
