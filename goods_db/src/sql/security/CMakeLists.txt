add_library(goods_db_sql_security STATIC
    auth_manager.cpp
)
target_include_directories(goods_db_sql_security PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_security PUBLIC
    goods_db_common
    goods_db_catalog
)
