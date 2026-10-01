include_guard(GLOBAL)

function(xgl_check_dependency target prefix)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Required dependency target is missing: ${target}")
    endif()
    get_target_property(version ${target} ${prefix}_VERSION)
    get_target_property(abi ${target} ${prefix}_ABI_VERSION)
    get_target_property(kind ${target} TYPE)
    set(expected_kind STATIC_LIBRARY)
    if(target STREQUAL "xgs::status")
        set(expected_kind INTERFACE_LIBRARY)
    endif()
    if(NOT version OR version VERSION_LESS 0.1.0 OR
       NOT version VERSION_LESS 0.2.0 OR NOT abi STREQUAL "1" OR
       NOT kind STREQUAL expected_kind)
        message(FATAL_ERROR
            "${target} requires version >=0.1.0,<0.2.0, ABI 1 and ${expected_kind}; got ${version}, ABI ${abi}, ${kind}")
    endif()

    # Public target names identify one package instance across all entry paths.
    get_property(recorded_version GLOBAL PROPERTY "XGL_DEPENDENCY_${prefix}_VERSION")
    if(recorded_version AND NOT version STREQUAL recorded_version)
        message(FATAL_ERROR
            "${target} has version ${version}, but ${prefix} targets already use ${recorded_version}; one package must use one version")
    endif()
    set_property(GLOBAL PROPERTY "XGL_DEPENDENCY_${prefix}_VERSION" "${version}")
endfunction()
