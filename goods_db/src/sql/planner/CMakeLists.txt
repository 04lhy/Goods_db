add_library(goods_db_sql_planner STATIC
    planner.cpp
)

target_include_directories(goods_db_sql_planner PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_planner PUBLIC
    goods_db_sql_binder
    goods_db_type
)
