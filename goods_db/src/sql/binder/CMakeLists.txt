add_library(goods_db_sql_binder STATIC
    binder.cpp
)

target_include_directories(goods_db_sql_binder PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_binder PUBLIC
    goods_db_sql_parser
    goods_db_catalog
    goods_db_type
    goods_db_storage_table
)
