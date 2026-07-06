add_library(goods_db_type STATIC
    value.cpp
    schema.cpp
)

target_include_directories(goods_db_type PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_type PUBLIC goods_db_common)
