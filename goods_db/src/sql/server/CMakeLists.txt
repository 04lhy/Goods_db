add_library(goods_db_sql_server STATIC
    thread_pool.cpp
    connection_handler.cpp
)
target_include_directories(goods_db_sql_server PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_server PUBLIC
    goods_db_sql_network
    goods_db_sql_protocol
    goods_db_sql_log
    goods_db_sql_security
    goods_db_common
)
