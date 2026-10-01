# Compile each header as the first include in an independent C++ translation unit.
# Including GoogleTest first would hide accidental C linkage around C++ headers.
file(GLOB header_contract_inputs CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/xgl/*.h"
    "${PROJECT_SOURCE_DIR}/include/xgl/internal/*.h")
set(header_contract_sources)
foreach(header IN LISTS header_contract_inputs)
    string(REPLACE "/" "_" unit_name "${header}")
    set(unit "${CMAKE_CURRENT_BINARY_DIR}/header-contracts/${unit_name}.cpp")
    file(GENERATE OUTPUT "${unit}" CONTENT "#include <${header}>\n")
    list(APPEND header_contract_sources "${unit}")
endforeach()
add_library(xgl_header_contracts OBJECT ${header_contract_sources})
target_link_libraries(xgl_header_contracts PRIVATE xgl ${XGL_PRIVATE_DEPENDENCIES})
target_compile_features(xgl_header_contracts PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(xgl_header_contracts PRIVATE /permissive-)
else()
    target_compile_options(xgl_header_contracts PRIVATE -pedantic-errors)
endif()
add_dependencies(xgl_tests xgl_header_contracts)
