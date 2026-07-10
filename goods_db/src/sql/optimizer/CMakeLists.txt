add_library(goods_db_sql_optimizer STATIC
    optimizer.cpp
)

target_include_directories(goods_db_sql_optimizer PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_optimizer PUBLIC
    goods_db_sql_planner
    goods_db_sql_binder
    goods_db_type
)
