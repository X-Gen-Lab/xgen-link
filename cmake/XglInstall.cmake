# Export only the public protocol SDK; private headers stay with their owners.
install(TARGETS xgl
    EXPORT xglTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set(XGL_PUBLIC_HEADERS
    include/xgl/xgl.h
    include/xgl/xgl_config.h
    include/xgl/xgl_error.h
    include/xgl/xgl_types.h
)

install(FILES ${XGL_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/xgl/xgl_build_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl)

install(EXPORT xglTargets
    FILE xglTargets.cmake
    NAMESPACE xgl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/xglConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/XglDependencyVersions.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)
