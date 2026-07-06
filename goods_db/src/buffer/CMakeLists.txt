add_library(goods_db_buffer STATIC
    buffer_pool_manager.cpp
    lru_k_replacer.cpp
    clock_replacer.cpp
    page_guard.cpp
)

target_include_directories(goods_db_buffer PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_buffer PUBLIC
    goods_db_common
    goods_db_type
    goods_db_storage_disk
    goods_db_storage_page
)
