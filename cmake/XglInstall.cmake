# Export only the public protocol SDK; private headers stay with their owners.
include(CMakePackageConfigHelpers)

install(
    TARGETS xgl
    EXPORT xglTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(XGL_PUBLIC_HEADERS
    include/xgl/xgl.h
    include/xgl/xgl_alloc.h
    include/xgl/xgl_config.h
    include/xgl/xgl_diagnostics.h
    include/xgl/xgl_error.h
    include/xgl/xgl_inplace.h
    include/xgl/xgl_runtime.h
    include/xgl/xgl_security.h
    include/xgl/xgl_types.h
)

install(FILES ${XGL_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/xgl/xgl_build_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl
)

install(
    EXPORT xglTargets
    FILE xglTargets.cmake
    NAMESPACE xgl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

# Keep expected package values independently of any existing consumer target.
get_target_property(exported_resources xgl EXPORT_PROPERTIES)
set(XGL_PACKAGE_RESOURCES "")
foreach(property IN LISTS exported_resources)
    get_target_property(value xgl "${property}")
    string(REGEX REPLACE "^XGL_" "xgl_" variable "${property}")
    string(APPEND XGL_PACKAGE_RESOURCES "set(${variable} \"${value}\")\n")
endforeach()

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/templates/xglConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)
