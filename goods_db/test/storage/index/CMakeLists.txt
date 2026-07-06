add_executable(index_test
    index_test.cpp
)
target_include_directories(index_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(index_test
    goods_db_storage_index
    goods_db_buffer
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME index_test COMMAND index_test)
