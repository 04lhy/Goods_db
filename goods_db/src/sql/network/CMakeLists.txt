add_library(goods_db_sql_network STATIC
    connection.cpp
    net_serv.cpp
)
target_include_directories(goods_db_sql_network PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_network PUBLIC
    goods_db_sql_protocol
)
