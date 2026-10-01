# Public API host examples with bounded application storage.
add_subdirectory(echo_server)
add_subdirectory(file_transfer)
if(XGL_FEATURE_FORWARDING)
    add_subdirectory(multi_node)
endif()
add_subdirectory(boot_update)
