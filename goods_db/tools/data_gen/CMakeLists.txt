add_executable(generate_test_data generate_test_data.cpp)
target_compile_features(generate_test_data PRIVATE cxx_std_17)

# Copy schema SQL to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/new_retail_schema.sql
    ${CMAKE_CURRENT_BINARY_DIR}/new_retail_schema.sql
    COPYONLY
)
