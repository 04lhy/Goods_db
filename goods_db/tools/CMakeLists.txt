# Tools: CLI administration and backup utilities

add_executable(goods_db_admin goods_db_admin.cpp)
target_include_directories(goods_db_admin PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
target_link_libraries(goods_db_admin)
target_compile_features(goods_db_admin PRIVATE cxx_std_17)

add_executable(goods_db_dump goods_db_dump.cpp)
target_include_directories(goods_db_dump PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
target_link_libraries(goods_db_dump)
target_compile_features(goods_db_dump PRIVATE cxx_std_17)
