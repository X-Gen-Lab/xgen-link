# Script-mode writer for the host archive measured by the xgl_footprint target.
foreach(argument IN ITEMS XGL_TARGET_FILE XGL_REPORT_FILE XGL_PROJECT_VERSION XGL_PROFILE XGL_BUILD_CONFIG)
    if(NOT DEFINED ${argument})
        message(FATAL_ERROR "${argument} is required")
    endif()
endforeach()

if(XGL_BUILD_CONFIG STREQUAL "")
    set(XGL_BUILD_CONFIG "unspecified")
endif()

file(SIZE "${XGL_TARGET_FILE}" XGL_ARCHIVE_BYTES)
get_filename_component(XGL_REPORT_DIR "${XGL_REPORT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${XGL_REPORT_DIR}")

file(WRITE "${XGL_REPORT_FILE}"
    "xgen-link archive size report\n"
    "version: ${XGL_PROJECT_VERSION}\n"
    "profile: ${XGL_PROFILE}\n"
    "configuration: ${XGL_BUILD_CONFIG}\n"
    "archive: ${XGL_TARGET_FILE}\n"
    "archive_bytes: ${XGL_ARCHIVE_BYTES}\n"
)
