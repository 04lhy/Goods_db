add_library(goods_db_sql_executor STATIC
    executor_factory.cpp
    advanced_executors.cpp
    execution_engine.cpp
)

target_include_directories(goods_db_sql_executor PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql_executor PUBLIC
    goods_db_sql_planner
    goods_db_catalog
    goods_db_storage_table
    goods_db_storage_index
    goods_db_buffer
    goods_db_storage_disk
    goods_db_storage_page
    goods_db_type
    goods_db_common
)
