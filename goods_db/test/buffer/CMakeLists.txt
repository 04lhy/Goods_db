add_executable(buffer_test
    buffer_test.cpp
)
target_include_directories(buffer_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(buffer_test
    goods_db_buffer
    goods_db_storage_disk
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME buffer_test COMMAND buffer_test)
