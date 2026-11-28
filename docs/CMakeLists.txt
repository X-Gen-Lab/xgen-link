# Use the same documentation entry from the SDK or a documentation-only build.
cmake_minimum_required(VERSION 3.24)
get_filename_component(
    XGL_SOURCE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ABSOLUTE
)
include("${XGL_SOURCE_DIRECTORY}/cmake/XglVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xgl_documentation VERSION "${XGL_VERSION}" LANGUAGES NONE)
endif()

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(XGL_DOCS_TOOL_PREFIX "" CACHE PATH "Prepared documentation tool prefix")
set(xgl_doc_args)
if(XGL_DOCS_TOOL_PREFIX)
    list(APPEND xgl_doc_args --tool-prefix "${XGL_DOCS_TOOL_PREFIX}")
endif()

add_custom_target(
    xgl_docs
    COMMAND
        "${Python3_EXECUTABLE}" -B
        "${XGL_SOURCE_DIRECTORY}/tools/documentation/cli.py" build --mode verify
        --language all --version "${XGL_VERSION}" --output
        "${CMAKE_CURRENT_BINARY_DIR}" ${xgl_doc_args}
    WORKING_DIRECTORY "${XGL_SOURCE_DIRECTORY}"
    COMMENT "Verifying the bilingual Sphinx and Doxygen documentation"
    VERBATIM
)
