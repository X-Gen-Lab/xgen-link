include("${CMAKE_CURRENT_LIST_DIR}/XglSources.cmake")

set(XGL_C_WARNINGS)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(XGL_C_WARNINGS
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Wno-unused-parameter
    )
elseif(MSVC)
    set(XGL_C_WARNINGS /W4 /WX /permissive-)
endif()

# The selected heap policy is local to this library and its generated header.
function(xgl_add_library target fallback_malloc generated_dir)
    set(XGL_ALLOW_FALLBACK_MALLOC "${fallback_malloc}")
    xgl_finalize_build_config("${generated_dir}")

    add_library(${target} STATIC ${XGL_SOURCES})
    target_compile_features(${target} PUBLIC c_std_11)
    target_compile_options(${target} PRIVATE ${XGL_C_WARNINGS})
    target_compile_definitions(${target} PUBLIC
        "XGL_EXPECTED_RESOURCE_CONFIG_ID=${XGL_RESOURCE_CONFIG_ID}U"
        "XGL_EXPECTED_CONFIG_ABI_VERSION=${XGL_CONFIG_ABI_VERSION}U"
        "XGL_ALLOW_FALLBACK_MALLOC=$<BOOL:${XGL_ALLOW_FALLBACK_MALLOC}>"
    )
    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${generated_dir}>"
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
    )

    set_target_properties(${target} PROPERTIES
        XGL_PROFILE "${XGL_PROFILE}"
        XGL_BUILD_CONFIG_ID "${XGL_BUILD_CONFIG_ID}"
        XGL_CAPABILITIES "${XGL_CAPABILITIES}"
        XGL_CONFIG_ABI_VERSION "${XGL_CONFIG_ABI_VERSION}"
        XGL_RESOURCE_CONFIG_ID "${XGL_RESOURCE_CONFIG_ID}"
        XGL_DATALINK_MAX_FRAME_SIZE "${XGL_DATALINK_MAX_FRAME_SIZE}"
        XGL_RELIABLE_INDEX_BUCKETS "${XGL_RELIABLE_BUCKET_COUNT}"
        XGL_TRANSPORT_WINDOW_CAPACITY "${XGL_BUILD_WINDOW_CAPACITY}"
        XGL_ENABLE_ADAPTIVE_RTO "${XGL_FEATURE_ADAPTIVE_RTO}"
        XGL_ENABLE_ADVANCED_RUNTIME "${XGL_FEATURE_ADVANCED_RUNTIME}"
        INTERFACE_XGL_RESOURCE_CONFIG_ID "${XGL_RESOURCE_CONFIG_ID}"
        INTERFACE_XGL_CONFIG_ABI_VERSION "${XGL_CONFIG_ABI_VERSION}"
    )
    set_property(TARGET ${target} PROPERTY EXPORT_PROPERTIES
        XGL_PROFILE XGL_BUILD_CONFIG_ID XGL_CAPABILITIES
        XGL_CONFIG_ABI_VERSION XGL_RESOURCE_CONFIG_ID XGL_DATALINK_MAX_FRAME_SIZE
        XGL_RELIABLE_INDEX_BUCKETS
        XGL_TRANSPORT_WINDOW_CAPACITY XGL_ENABLE_ADAPTIVE_RTO XGL_ENABLE_ADVANCED_RUNTIME)
    set_property(TARGET ${target} PROPERTY COMPATIBLE_INTERFACE_STRING
        XGL_RESOURCE_CONFIG_ID XGL_CONFIG_ABI_VERSION)
endfunction()
