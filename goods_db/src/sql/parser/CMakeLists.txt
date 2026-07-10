add_library(goods_db_sql_parser STATIC
    parser.cpp
)

target_include_directories(goods_db_sql_parser PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_parser PUBLIC
    duckdb_pg_query
    goods_db_type
)
