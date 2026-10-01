# The parent supplies targets or prepared packages. Source assembly belongs to
# the consuming project or the explicit development entry in dev/.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/XglDependencyVersions.cmake")

function(xgl_provide_dependency name prefix namespace)
    set(source_option "XGL_${name}_SOURCE_DIR")
    if(DEFINED ${source_option})
        message(FATAL_ERROR
            "${source_option} is no longer a production dependency entry. "
            "Provide dependency targets or installed packages; for standalone "
            "source development use dev/ with XGL_DEV_${name}_SOURCE_DIR. "
            "Remove this legacy option from an existing CMake cache.")
    endif()
    string(TOLOWER "${name}" package_name)
    set(components ${ARGN})
    set(missing FALSE)
    foreach(component IN LISTS components)
        if(NOT TARGET ${namespace}::${component})
            set(missing TRUE)
        endif()
    endforeach()
    if(missing)
        find_package(xgen_${package_name} 0.1 CONFIG REQUIRED COMPONENTS ${components})
    endif()
    foreach(component IN LISTS components)
        xgl_check_dependency(${namespace}::${component} ${prefix})
    endforeach()
endfunction()

if(DEFINED XGL_CORE_SOURCE_DIR OR TARGET xgc::base)
    message(FATAL_ERROR "The xgen-core integration has been removed; provide the independent component packages")
endif()

xgl_provide_dependency(STATUS XGS xgs status)
xgl_provide_dependency(BYTES XGB xgb bytes)
xgl_provide_dependency(CRC XGCRC xgcrc crc16)
set(memory_components allocator pool size_class)
if(XGL_ALLOW_FALLBACK_MALLOC OR XGL_BUILD_TESTS)
    list(APPEND memory_components libc_allocator)
endif()
xgl_provide_dependency(MEMORY XGM xgm ${memory_components})
set(container_components list bitset)
if(XGL_FEATURE_ROUTE_INDEX)
    list(APPEND container_components hash)
endif()
xgl_provide_dependency(CONTAINERS XGCT xgct ${container_components})
set(XGL_PRIVATE_DEPENDENCIES xgm::pool xgm::size_class
    xgct::list xgct::bitset xgb::bytes xgcrc::crc16)
if(XGL_FEATURE_ROUTE_INDEX)
    list(APPEND XGL_PRIVATE_DEPENDENCIES xgct::hash)
endif()
