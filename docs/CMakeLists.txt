# Build the same site from the repository or a documentation-only build.
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xgl_documentation LANGUAGES NONE)
endif()

find_package(Doxygen REQUIRED)
find_program(XGL_MKDOCS_EXE NAMES mkdocs mkdocs.exe REQUIRED)

get_filename_component(XGL_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(XGL_DOC_VERSION "${PROJECT_VERSION}")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(XGL_PROJECT_FILE "${XGL_SOURCE_DIRECTORY}/CMakeLists.txt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XGL_PROJECT_FILE}")
    file(STRINGS "${XGL_PROJECT_FILE}" XGL_VERSION_LINE
        REGEX "^[ \t]+VERSION[ \t]+[0-9]+\\.[0-9]+\\.[0-9]+" LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" XGL_DOC_VERSION "${XGL_VERSION_LINE}")
    if(XGL_DOC_VERSION STREQUAL "")
        message(FATAL_ERROR "The xgl project does not declare a documentation version")
    endif()
endif()

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
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XGL_DOXYGEN_OUTPUT_DIR}
    COMMAND ${DOXYGEN_EXECUTABLE} ${XGL_DOXYFILE}
    COMMAND ${XGL_MKDOCS_EXE}
        build
        --strict
        --clean
        --config-file ${XGL_SOURCE_DIRECTORY}/mkdocs.yml
        --site-dir ${XGL_DOCS_SITE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XGL_DOCS_SITE_DIR}/api/doxygen
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${XGL_DOXYGEN_OUTPUT_DIR}/html
        ${XGL_DOCS_SITE_DIR}/api/doxygen/html
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${XGL_DOXYGEN_OUTPUT_DIR}/html/index.html
        ${XGL_DOCS_SITE_DIR}/api/doxygen/html/index.html
    WORKING_DIRECTORY ${XGL_SOURCE_DIRECTORY}
    COMMENT "Building MkDocs bilingual documentation and Doxygen public API reference"
    VERBATIM
)
