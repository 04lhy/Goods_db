# Parser unit test
add_executable(parser_test
    parser_test.cpp
)
target_include_directories(parser_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(parser_test
    goods_db_sql_parser
    goods_db_type
    gtest gtest_main fmt::fmt
)
add_test(NAME parser_test COMMAND parser_test)

# Binder + Planner + Executor pipeline test (Day 2)
add_executable(binder_test
    binder_test.cpp
)
target_include_directories(binder_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(binder_test
    goods_db_sql_executor
    goods_db_sql_planner
    goods_db_sql_binder
    goods_db_sql_parser
    goods_db_sql
    goods_db_catalog
    goods_db_storage_table
    goods_db_storage_index
    goods_db_buffer
    goods_db_storage_disk
    goods_db_storage_page
    goods_db_type
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME binder_test COMMAND binder_test)

# Integration test
add_executable(integration_test
    integration_test.cpp
)
target_include_directories(integration_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(integration_test
    goods_db_sql
    goods_db_catalog
    goods_db_storage_table
    goods_db_storage_index
    goods_db_buffer
    goods_db_storage_disk
    goods_db_storage_page
    goods_db_type
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME integration_test COMMAND integration_test)
