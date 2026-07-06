add_library(goods_db_storage_page STATIC
    table_page.cpp
)

target_include_directories(goods_db_storage_page PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_storage_page PUBLIC goods_db_common goods_db_type)
