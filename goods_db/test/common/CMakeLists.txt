add_library(goods_db_test_common STATIC
    test_common.h
)
target_include_directories(goods_db_test_common PUBLIC ${PROJECT_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(goods_db_test_common PUBLIC goods_db_common)
