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
