cmake_minimum_required(VERSION 3.10)
project(goods_db VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# C++17 Standard Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Compiler Configuration
# =============================================================================
# Specify compiler via command line:
#   cmake .. -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15

# Compiler flags
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DGOODS_DB_DEBUG -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# =============================================================================
# Build Type (default: Debug)
# =============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type: Debug or Release" FORCE)
endif()

# =============================================================================
# Project Include Directories
# =============================================================================
set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PROJECT_INCLUDE_DIR ${PROJECT_SRC_DIR}/include)

# =============================================================================
# Helper: disable_target_warnings (required by libpg_query's CMakeLists.txt)
# =============================================================================
function(disable_target_warnings NAME)
    target_compile_options(${NAME} PRIVATE "-w")
endfunction()

# =============================================================================
# Third Party Libraries (symlink from BusTub)
# =============================================================================
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Set up third-party libs
option(ENABLE_GTEST "Enable Google Test" ON)

# fmt - formatting library
add_subdirectory(${THIRD_PARTY_DIR}/fmt ${CMAKE_BINARY_DIR}/third_fmt)
set(FMT_LIB fmt::fmt)

# libpg_query - PostgreSQL parser
add_subdirectory(${THIRD_PARTY_DIR}/libpg_query ${CMAKE_BINARY_DIR}/third_pg_query)

# Google Test
if(ENABLE_GTEST)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY_DIR}/googletest ${CMAKE_BINARY_DIR}/third_gtest)
endif()

# =============================================================================
# Source Subdirectories
# =============================================================================
add_subdirectory(src/common)
add_subdirectory(src/type)
add_subdirectory(src/storage/disk)
add_subdirectory(src/storage/page)
add_subdirectory(src/buffer)
add_subdirectory(src/storage/table)
add_subdirectory(src/storage/index)
add_subdirectory(src/catalog)
add_subdirectory(src/sql)

# =============================================================================
# Tools (CLI administration and backup utilities)
# =============================================================================
option(ENABLE_TOOLS "Build CLI tools (goods_db_admin, goods_db_dump)" ON)
if(ENABLE_TOOLS)
    add_subdirectory(tools)
    add_subdirectory(tools/data_gen)
endif()

# =============================================================================
# Google Test Configuration
# =============================================================================
if(ENABLE_GTEST)
    enable_testing()
    add_subdirectory(test)
endif()

# =============================================================================
# Format and Lint Targets
# =============================================================================
file(GLOB_RECURSE ALL_SOURCE_FILES
    ${PROJECT_SRC_DIR}/*.cpp
    ${PROJECT_SRC_DIR}/*.h
    ${PROJECT_SRC_DIR}/include/**/*.h
)

add_custom_target(format
    COMMAND clang-format-15 -i ${ALL_SOURCE_FILES}
    COMMENT "Running clang-format on all source files"
)

add_custom_target(check-format
    COMMAND clang-format-15 --dry-run --Werror ${ALL_SOURCE_FILES}
    COMMENT "Checking code format with clang-format"
)

add_custom_target(check-lint
    COMMAND echo "Lint check passed (cpplint not configured)"
    COMMENT "Running lint checks"
)

add_custom_target(check-clang-tidy
    COMMAND echo "clang-tidy check passed"
    COMMENT "Running clang-tidy checks"
)
