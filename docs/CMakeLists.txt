# Documentation build integration for MkDocs + Doxygen.

find_package(Doxygen REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(XGL_MKDOCS_EXE NAMES mkdocs mkdocs.exe REQUIRED)

set(XGL_DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
set(XGL_DOCS_SITE_DIR ${CMAKE_CURRENT_BINARY_DIR}/site)
set(XGL_DOXYFILE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
    ${XGL_DOXYFILE}
    @ONLY
)

add_custom_target(xgl_docs
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${XGL_DOXYGEN_OUTPUT_DIR}
    COMMAND ${DOXYGEN_EXECUTABLE} ${XGL_DOXYFILE}
    COMMAND ${XGL_MKDOCS_EXE}
        build
        --strict
        --clean
        --config-file ${CMAKE_SOURCE_DIR}/mkdocs.yml
        --site-dir ${XGL_DOCS_SITE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XGL_DOCS_SITE_DIR}/api/doxygen
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${XGL_DOXYGEN_OUTPUT_DIR}/html
        ${XGL_DOCS_SITE_DIR}/api/doxygen/html
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building MkDocs bilingual documentation and Doxygen public API reference"
    VERBATIM
)
