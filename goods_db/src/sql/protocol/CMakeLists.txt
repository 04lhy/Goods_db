add_library(goods_db_sql_protocol STATIC
    protocol.cpp
    protocol_text.cpp
    protocol_binary.cpp
)
target_include_directories(goods_db_sql_protocol PUBLIC ${PROJECT_INCLUDE_DIR})
