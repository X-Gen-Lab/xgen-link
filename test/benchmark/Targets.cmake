set(benchmark_support "${CMAKE_CURRENT_LIST_DIR}/../support")

add_library(xgl_benchmark_phy OBJECT "${benchmark_support}/mocks/mock_phy.cpp")
xgl_target_owned_warnings(xgl_benchmark_phy)
target_link_libraries(xgl_benchmark_phy PUBLIC xgl::xgl GTest::gmock)
target_include_directories(
    xgl_benchmark_phy
    PUBLIC "${benchmark_support}" "${benchmark_support}/mocks"
)

set(benchmarks bench_throughput bench_latency bench_memory)
foreach(benchmark IN LISTS benchmarks)
    add_executable(${benchmark} "${CMAKE_CURRENT_LIST_DIR}/${benchmark}.cpp")
    xgl_target_owned_warnings(${benchmark})
    target_link_libraries(${benchmark} PRIVATE xgl_benchmark_phy)
endforeach()

add_custom_target(xgl_benchmarks DEPENDS ${benchmarks})
