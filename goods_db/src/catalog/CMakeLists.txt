add_library(goods_db_catalog STATIC
    catalog.cpp
)

target_include_directories(goods_db_catalog PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_catalog PUBLIC
    goods_db_common
    goods_db_type
    goods_db_buffer
    goods_db_storage_table
    goods_db_storage_index
)
