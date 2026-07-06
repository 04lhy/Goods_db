add_library(goods_db_storage_table STATIC
    tuple.cpp
    table_heap.cpp
    table_iterator.cpp
)

target_include_directories(goods_db_storage_table PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_storage_table PUBLIC
    goods_db_common
    goods_db_type
    goods_db_storage_disk
    goods_db_storage_page
    goods_db_buffer
)
