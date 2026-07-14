if (ENABLE_TESTS)
    add_subdirectory(tests)
endif()

if (ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if (ENABLE_FUZZING)
    add_subdirectory(fuzzers)
endif()

# task29: standalone CH native HashJoin benchmark driver (no query engine).
clickhouse_add_executable(ch_hj_bench examples/ch_hj_bench.cpp)
target_link_libraries(ch_hj_bench PRIVATE ch_contrib::gbenchmark_all dbms clickhouse_common_io)
