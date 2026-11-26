set(XGL_CONFIG_ABI_VERSION ${PROJECT_VERSION_MAJOR})
set(XGL_PROFILE "full" CACHE STRING "Compiled protocol profile: boot, embedded-lite, embedded, full")
set_property(CACHE XGL_PROFILE PROPERTY STRINGS boot embedded-lite embedded full)
if(XGL_PROFILE STREQUAL "boot")
    set(XGL_FEATURE_AUTH 0)
    set(XGL_FEATURE_ROUTE_INDEX 0)
    set(XGL_SECURITY_SESSION_CAPACITY 0)
    set(XGL_FEATURE_FORWARDING 0)
    set(XGL_FEATURE_FRAGMENTATION 0)
    set(XGL_FEATURE_OUT_OF_ORDER 0)
    set(XGL_FEATURE_DIAGNOSTICS 0)
    set(XGL_FEATURE_STATISTICS 0)
    set(XGL_RELIABLE_BUCKET_COUNT 1)
    set(XGL_BUILD_CONFIG_ID 301)
elseif(XGL_PROFILE STREQUAL "embedded" OR XGL_PROFILE STREQUAL "full" OR
       XGL_PROFILE STREQUAL "embedded-lite")
    set(XGL_FEATURE_AUTH 1)
    set(XGL_FEATURE_ROUTE_INDEX 1)
    set(XGL_SECURITY_SESSION_CAPACITY 16)
    set(XGL_FEATURE_FORWARDING 1)
    set(XGL_FEATURE_FRAGMENTATION 1)
    set(XGL_FEATURE_OUT_OF_ORDER 1)
    set(XGL_FEATURE_DIAGNOSTICS 1)
    set(XGL_FEATURE_STATISTICS 1)
    set(XGL_RELIABLE_BUCKET_COUNT 32)
    if(XGL_PROFILE STREQUAL "embedded-lite")
        set(XGL_FEATURE_AUTH 0)
        set(XGL_FEATURE_FORWARDING 0)
        set(XGL_SECURITY_SESSION_CAPACITY 0)
        set(XGL_RELIABLE_BUCKET_COUNT 8)
        set(XGL_BUILD_CONFIG_ID 304)
    elseif(XGL_PROFILE STREQUAL "embedded")
        set(XGL_BUILD_CONFIG_ID 302)
        set(XGL_SECURITY_SESSION_CAPACITY 4)
    else()
        set(XGL_BUILD_CONFIG_ID 303)
    endif()
else()
    message(FATAL_ERROR "Unknown XGL_PROFILE '${XGL_PROFILE}'")
endif()
set(XGL_DATALINK_RX_CHUNK_SIZE "AUTO" CACHE STRING
    "Temporary PHY read buffer bytes: AUTO, 16, 32, 64, 128")
set_property(CACHE XGL_DATALINK_RX_CHUNK_SIZE PROPERTY STRINGS AUTO 16 32 64 128)
if(XGL_DATALINK_RX_CHUNK_SIZE STREQUAL "AUTO")
    if(XGL_PROFILE STREQUAL "boot")
        set(XGL_BUILD_RX_CHUNK_SIZE 32)
    else()
        set(XGL_BUILD_RX_CHUNK_SIZE 128)
    endif()
elseif(XGL_DATALINK_RX_CHUNK_SIZE MATCHES "^(16|32|64|128)$")
    set(XGL_BUILD_RX_CHUNK_SIZE "${XGL_DATALINK_RX_CHUNK_SIZE}")
else()
    message(FATAL_ERROR "XGL_DATALINK_RX_CHUNK_SIZE must be AUTO, 16, 32, 64 or 128")
endif()
set(XGL_RELIABLE_INDEX_BUCKETS "AUTO" CACHE STRING
    "Reliable index buckets per peer: AUTO, 1, 2, 4, 8, 16, 32")
set_property(CACHE XGL_RELIABLE_INDEX_BUCKETS PROPERTY STRINGS AUTO 1 2 4 8 16 32)
if(NOT XGL_RELIABLE_INDEX_BUCKETS STREQUAL "AUTO")
    if(NOT XGL_RELIABLE_INDEX_BUCKETS MATCHES "^(1|2|4|8|16|32)$")
        message(FATAL_ERROR "XGL_RELIABLE_INDEX_BUCKETS must be AUTO, 1, 2, 4, 8, 16 or 32")
    endif()
    set(XGL_RELIABLE_BUCKET_COUNT "${XGL_RELIABLE_INDEX_BUCKETS}")
endif()

set(XGL_TRANSPORT_WINDOW_CAPACITY "AUTO" CACHE STRING
    "Compiled private window capacity: AUTO (128), 1, 8, 16, 32, 64, 128")
set_property(CACHE XGL_TRANSPORT_WINDOW_CAPACITY PROPERTY STRINGS AUTO 1 8 16 32 64 128)
if(XGL_TRANSPORT_WINDOW_CAPACITY STREQUAL "AUTO")
    # Preserve the historical private window contract in every profile.
    set(XGL_BUILD_WINDOW_CAPACITY 128)
elseif(XGL_TRANSPORT_WINDOW_CAPACITY MATCHES "^(1|8|16|32|64|128)$")
    set(XGL_BUILD_WINDOW_CAPACITY "${XGL_TRANSPORT_WINDOW_CAPACITY}")
else()
    message(FATAL_ERROR "XGL_TRANSPORT_WINDOW_CAPACITY must be AUTO, 1, 8, 16, 32, 64 or 128")
endif()

option(XGL_ENABLE_ADAPTIVE_RTO "Include per-peer RTT estimator state and sampling" ON)
string(TOUPPER "${XGL_ENABLE_ADAPTIVE_RTO}" adaptive_rto_option)
if(adaptive_rto_option MATCHES "^(1|ON|YES|TRUE|Y)$")
    set(XGL_FEATURE_ADAPTIVE_RTO 1)
elseif(adaptive_rto_option MATCHES "^(0|OFF|NO|FALSE|N)$")
    set(XGL_FEATURE_ADAPTIVE_RTO 0)
else()
    message(FATAL_ERROR "XGL_ENABLE_ADAPTIVE_RTO must be ON or OFF")
endif()
unset(adaptive_rto_option)

option(XGL_ENABLE_ADVANCED_RUNTIME "Enable explicit runtime limits, scoped observation and message storage options" OFF)
string(TOUPPER "${XGL_ENABLE_ADVANCED_RUNTIME}" advanced_runtime_option)
if(advanced_runtime_option MATCHES "^(1|ON|YES|TRUE|Y)$")
    set(XGL_FEATURE_ADVANCED_RUNTIME 1)
elseif(advanced_runtime_option MATCHES "^(0|OFF|NO|FALSE|N)$")
    set(XGL_FEATURE_ADVANCED_RUNTIME 0)
else()
    message(FATAL_ERROR "XGL_ENABLE_ADVANCED_RUNTIME must be ON or OFF")
endif()
unset(advanced_runtime_option)

set(XGL_CAPABILITIES wire reliable)
foreach(capability AUTH FORWARDING FRAGMENTATION OUT_OF_ORDER STATISTICS DIAGNOSTICS ADVANCED_RUNTIME)
    if(XGL_FEATURE_${capability})
        if(capability STREQUAL "AUTH")
            list(APPEND XGL_CAPABILITIES authentication)
        else()
            string(TOLOWER "${capability}" capability_name)
            list(APPEND XGL_CAPABILITIES "${capability_name}")
        endif()
    endif()
endforeach()

# Finalize after the root has resolved fallback malloc. The identity is a
# collision-free mixed-radix encoding of all compiled capacity selections.
function(xgl_finalize_build_config)
    set(bucket_values 1 2 4 8 16 32)
    set(window_values 1 8 16 32 64 128)
    set(chunk_values 16 32 64 128)
    list(FIND bucket_values "${XGL_RELIABLE_BUCKET_COUNT}" bucket_code)
    list(FIND window_values "${XGL_BUILD_WINDOW_CAPACITY}" window_code)
    list(FIND chunk_values "${XGL_BUILD_RX_CHUNK_SIZE}" chunk_code)
    if(XGL_ALLOW_FALLBACK_MALLOC)
        set(heap_code 1)
    else()
        set(heap_code 0)
    endif()
    math(EXPR profile_code "${XGL_BUILD_CONFIG_ID} - 300")
    math(EXPR resource_id
        "${profile_code} + 8 * (${bucket_code} + 8 * (${window_code} + 8 * (${XGL_FEATURE_ADAPTIVE_RTO} + 2 * (${chunk_code} + 4 * (${heap_code} + 2 * ${XGL_FEATURE_ADVANCED_RUNTIME})))))")

    set(default_buckets 32)
    set(default_chunk 128)
    set(default_heap 0)
    if(XGL_PROFILE STREQUAL "boot")
        set(default_buckets 1)
        set(default_chunk 32)
    elseif(XGL_PROFILE STREQUAL "embedded-lite")
        set(default_buckets 8)
    elseif(XGL_PROFILE STREQUAL "full")
        set(default_heap 1)
    endif()
    if(NOT (XGL_RELIABLE_BUCKET_COUNT EQUAL default_buckets AND
            XGL_BUILD_WINDOW_CAPACITY EQUAL 128 AND
            XGL_FEATURE_ADAPTIVE_RTO EQUAL 1 AND
            XGL_BUILD_RX_CHUNK_SIZE EQUAL default_chunk AND
            heap_code EQUAL default_heap AND
            XGL_FEATURE_ADVANCED_RUNTIME EQUAL 0))
        math(EXPR XGL_BUILD_CONFIG_ID "65536 + ${resource_id}")
    endif()
    set(XGL_RESOURCE_CONFIG_ID "${resource_id}" PARENT_SCOPE)
    set(XGL_BUILD_CONFIG_ID "${XGL_BUILD_CONFIG_ID}" PARENT_SCOPE)
    set(XGL_RESOURCE_CONFIG_ID "${resource_id}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xgl_build_config.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/xgl/xgl_build_config.h" @ONLY)
endfunction()

function(xgl_apply_build_contract target)
    set_target_properties(${target} PROPERTIES
        XGL_CONFIG_ABI_VERSION "${XGL_CONFIG_ABI_VERSION}"
        XGL_RESOURCE_CONFIG_ID "${XGL_RESOURCE_CONFIG_ID}"
        XGL_RELIABLE_INDEX_BUCKETS "${XGL_RELIABLE_BUCKET_COUNT}"
        XGL_TRANSPORT_WINDOW_CAPACITY "${XGL_BUILD_WINDOW_CAPACITY}"
        XGL_ENABLE_ADAPTIVE_RTO "${XGL_FEATURE_ADAPTIVE_RTO}"
        XGL_ENABLE_ADVANCED_RUNTIME "${XGL_FEATURE_ADVANCED_RUNTIME}"
        INTERFACE_XGL_RESOURCE_CONFIG_ID "${XGL_RESOURCE_CONFIG_ID}"
        INTERFACE_XGL_CONFIG_ABI_VERSION "${XGL_CONFIG_ABI_VERSION}")
    set_property(TARGET ${target} APPEND PROPERTY EXPORT_PROPERTIES
        XGL_CONFIG_ABI_VERSION XGL_RESOURCE_CONFIG_ID XGL_RELIABLE_INDEX_BUCKETS
        XGL_TRANSPORT_WINDOW_CAPACITY XGL_ENABLE_ADAPTIVE_RTO XGL_ENABLE_ADVANCED_RUNTIME)
    set_property(TARGET ${target} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING
        XGL_RESOURCE_CONFIG_ID XGL_CONFIG_ABI_VERSION)
    target_compile_definitions(${target} PUBLIC
        "XGL_EXPECTED_RESOURCE_CONFIG_ID=${XGL_RESOURCE_CONFIG_ID}U"
        "XGL_EXPECTED_CONFIG_ABI_VERSION=${XGL_CONFIG_ABI_VERSION}U")
endfunction()
