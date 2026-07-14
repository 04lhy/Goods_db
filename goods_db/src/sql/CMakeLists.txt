add_subdirectory(parser)
add_subdirectory(binder)
add_subdirectory(planner)
add_subdirectory(executor)
add_subdirectory(optimizer)
add_subdirectory(protocol)
add_subdirectory(network)
add_subdirectory(server)
add_subdirectory(log)
add_subdirectory(security)

add_library(goods_db_sql STATIC
    handlerton.cpp
    goods_handler.cpp
    lock_manager.cpp
    transaction_manager.cpp
)

target_include_directories(goods_db_sql PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_sql PUBLIC
    goods_db_sql_parser
    goods_db_sql_binder
    goods_db_sql_planner
    goods_db_sql_executor
    goods_db_sql_optimizer
    goods_db_sql_protocol
    goods_db_sql_network
    goods_db_sql_server
    goods_db_sql_log
    goods_db_sql_security
    goods_db_common
    goods_db_type
    goods_db_storage_disk
    goods_db_storage_page
    goods_db_buffer
    goods_db_storage_table
    goods_db_storage_index
    goods_db_catalog
)

# =============================================================================
# goods_db_binlog tool
# =============================================================================
add_executable(goods_db_binlog
    binlog/goods_db_binlog.cpp
)
target_include_directories(goods_db_binlog PRIVATE ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_binlog PRIVATE
    goods_db_sql_log
    goods_db_common
)

# =============================================================================
# goods_db server executable
# =============================================================================
add_executable(goods_db_server
    server/goods_db_main.cpp
)
target_include_directories(goods_db_server PRIVATE ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_server PRIVATE
    goods_db_sql
    goods_db_sql_protocol
    goods_db_sql_network
    goods_db_sql_server
    goods_db_sql_log
    goods_db_sql_security
)
