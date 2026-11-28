include("${CMAKE_CURRENT_LIST_DIR}/XglSources.cmake")

function(xgl_target_owned_warnings target)
    foreach(language IN ITEMS C CXX)
        if(
            CMAKE_${language}_COMPILER_ID STREQUAL "MSVC"
            OR CMAKE_${language}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
        )
            set(warnings /W4 /WX)
        elseif(CMAKE_${language}_COMPILER_ID MATCHES "GNU|Clang")
            set(warnings
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
                -Werror
            )
            if(language STREQUAL "C")
                list(APPEND warnings -Wstrict-prototypes -Wmissing-prototypes)
            endif()
        else()
            continue()
        endif()
        target_compile_options(
            ${target}
            PRIVATE "$<$<COMPILE_LANGUAGE:${language}>:${warnings}>"
        )
    endforeach()
endfunction()

set(XGL_C_WARNINGS)
if(MSVC)
    set(XGL_C_WARNINGS /permissive-)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(XGL_C_WARNINGS
        -Wsign-conversion
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Wno-unused-parameter
    )
endif()

function(xgl_export_resource_property target property value)
    set_property(TARGET ${target} PROPERTY ${property} "${value}")
    set_property(TARGET ${target} APPEND PROPERTY EXPORT_PROPERTIES ${property})
endfunction()

# The selected heap policy is local to this library and its generated header.
function(xgl_add_library target fallback_malloc generated_dir)
    set(XGL_ALLOW_FALLBACK_MALLOC "${fallback_malloc}")
    xgl_finalize_build_config("${generated_dir}")

    add_library(${target} STATIC ${XGL_SOURCES})
    target_compile_features(${target} PUBLIC c_std_11)
    xgl_target_owned_warnings(${target})
    target_compile_options(${target} PRIVATE ${XGL_C_WARNINGS})
    target_compile_definitions(
        ${target}
        PUBLIC
            "XGL_EXPECTED_RESOURCE_CONFIG_ID=${XGL_RESOURCE_CONFIG_ID}U"
            "XGL_ALLOW_FALLBACK_MALLOC=$<BOOL:${XGL_ALLOW_FALLBACK_MALLOC}>"
    )
    target_include_directories(
        ${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${generated_dir}>"
        PRIVATE "${PROJECT_SOURCE_DIR}/src"
    )

    xgl_export_resource_property(${target} XGL_PROFILE "${XGL_PROFILE}")
    xgl_export_resource_property(${target} XGL_CAPABILITIES "${XGL_CAPABILITIES}")
    xgl_export_resource_property(${target} XGL_RESOURCE_CONFIG_ID
        "${XGL_RESOURCE_CONFIG_ID}"
    )
    xgl_export_resource_property(${target} XGL_DATALINK_MAX_FRAME_SIZE
        "${XGL_DATALINK_MAX_FRAME_SIZE}"
    )
    xgl_export_resource_property(${target} XGL_RELIABLE_INDEX_BUCKETS
        "${XGL_RELIABLE_BUCKET_COUNT}"
    )
    xgl_export_resource_property(${target} XGL_TRANSPORT_WINDOW_CAPACITY
        "${XGL_BUILD_WINDOW_CAPACITY}"
    )
    xgl_export_resource_property(${target} XGL_ENABLE_ADAPTIVE_RTO
        "${XGL_FEATURE_ADAPTIVE_RTO}"
    )
    xgl_export_resource_property(${target} XGL_ENABLE_ADVANCED_RUNTIME
        "${XGL_FEATURE_ADVANCED_RUNTIME}"
    )
    set_property(
        TARGET ${target}
        PROPERTY INTERFACE_XGL_RESOURCE_CONFIG_ID "${XGL_RESOURCE_CONFIG_ID}"
    )
    set_property(
        TARGET ${target}
        PROPERTY COMPATIBLE_INTERFACE_STRING XGL_RESOURCE_CONFIG_ID
    )
endfunction()
