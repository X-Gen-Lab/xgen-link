# Compile every public and private header first in separate C11/C++17 units.
# Framework or neighboring headers must not hide missing includes or linkage.
file(GLOB public_header_inputs CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/xgl/*.h")
file(GLOB_RECURSE private_header_inputs CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/*.h")
list(APPEND public_header_inputs xgl/xgl_build_config.h)
set(header_contract_sources)
foreach(header IN LISTS public_header_inputs private_header_inputs)
    string(REPLACE "/" "_" unit_name "${header}")
    foreach(language c cpp)
        set(unit "${CMAKE_CURRENT_BINARY_DIR}/header-contracts/${unit_name}.${language}")
        # Macro-only configuration headers otherwise form an empty C unit.
        file(GENERATE OUTPUT "${unit}" CONTENT
            "#include <${header}>\n\ntypedef int xgl_header_contract_translation_unit;\n")
        list(APPEND header_contract_sources "${unit}")
    endforeach()
endforeach()
add_library(xgl_header_contracts OBJECT ${header_contract_sources})
target_link_libraries(xgl_header_contracts PRIVATE xgl ${XGL_PRIVATE_DEPENDENCIES})
target_include_directories(xgl_header_contracts PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(xgl_header_contracts PRIVATE c_std_11 cxx_std_17)
set_target_properties(xgl_header_contracts PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(xgl_header_contracts PRIVATE /permissive-)
else()
    target_compile_options(xgl_header_contracts PRIVATE -pedantic-errors)
endif()
add_dependencies(xgl_tests xgl_header_contracts)
