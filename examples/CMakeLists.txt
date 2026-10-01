# Public API host examples with bounded application storage.
add_subdirectory(echo_server)
add_subdirectory(file_transfer)
if(NOT XGL_PROFILE STREQUAL "boot")
    add_subdirectory(multi_node)
endif()
add_subdirectory(boot_update)
