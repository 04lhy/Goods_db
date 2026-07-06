add_executable(table_test
    table_test.cpp
)
target_include_directories(table_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(table_test
    goods_db_storage_table
    goods_db_storage_page
    goods_db_storage_disk
    goods_db_buffer
    goods_db_type
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME table_test COMMAND table_test)
