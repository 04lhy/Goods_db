add_library(goods_db_storage_disk STATIC
    disk_manager.cpp
)

target_include_directories(goods_db_storage_disk PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(goods_db_storage_disk PUBLIC goods_db_common goods_db_type)
