add_library(goods_db_sql_log STATIC
    error_log.cpp
    query_log.cpp
    binary_log.cpp
    log_manager.cpp
)
target_include_directories(goods_db_sql_log PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_log PUBLIC
    goods_db_common
)
