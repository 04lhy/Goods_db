add_library(goods_db_common STATIC
    logger.cpp
    config_manager.cpp
)

target_include_directories(goods_db_common PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_common PUBLIC ${FMT_LIB})
