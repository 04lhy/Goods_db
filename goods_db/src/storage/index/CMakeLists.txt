add_library(goods_db_storage_index STATIC
    b_plus_tree.cpp
    extendible_hash_index.cpp
)

target_include_directories(goods_db_storage_index PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_storage_index PUBLIC
    goods_db_common
    goods_db_type
    goods_db_storage_page
    goods_db_buffer
)
