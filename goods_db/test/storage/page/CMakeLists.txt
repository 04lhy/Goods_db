add_executable(page_test
    page_test.cpp
)
target_include_directories(page_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(page_test
    goods_db_storage_page
    goods_db_storage_disk
    goods_db_buffer
    goods_db_common
    goods_db_test_common
    gtest gtest_main fmt::fmt
)
add_test(NAME page_test COMMAND page_test)
