add_executable(type_test
    type_test.cpp
)
target_include_directories(type_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(type_test
    goods_db_type
    goods_db_storage_table
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME type_test COMMAND type_test)
