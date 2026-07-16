cmake_minimum_required(VERSION 3.16)
project(goods_db_studio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# =============================================================================
# Web Server target (goods_db_web) — always built, no Qt dependency
# =============================================================================

set(WEB_SOURCES
    src/server/main_web.cpp
    src/server/web_server.cpp
    src/server/api_handler.cpp
    src/network/goods_db_protocol.cpp
)

set(WEB_HEADERS
    src/server/web_server.h
    src/server/api_handler.h
    src/network/goods_db_protocol.h
)

add_executable(goods_db_web ${WEB_SOURCES} ${WEB_HEADERS})

target_include_directories(goods_db_web PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
)

target_link_libraries(goods_db_web PRIVATE
    pthread
)

# =============================================================================
# Qt6 Desktop target (goods_db_studio) — optionally built if Qt6 is available
# =============================================================================

find_package(Qt6 QUIET COMPONENTS Widgets Sql Network)

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    set(STUDIO_SOURCES
        src/main.cpp
        src/main_window.cpp
        src/network/goods_db_client.cpp
        src/network/database_worker.cpp
        src/network/protocol_text_adapter.cpp
        src/connection/connection_dialog.cpp
        src/connection/connection_pool.cpp
        src/sql_editor/sql_highlighter.cpp
        src/sql_editor/sql_editor_widget.cpp
        src/sql_editor/query_executor.cpp
        src/sql_editor/query_history.cpp
        src/result_view/result_table_model.cpp
        src/result_view/result_table_view.cpp
        src/result_view/result_pagination.cpp
        src/result_view/export_dialog.cpp
        src/object_tree/object_tree_model.cpp
        src/object_tree/object_tree_widget.cpp
        src/object_tree/table_info_panel.cpp
        src/ui/admin_panel.cpp
        src/ui/backup_wizard.cpp
        src/ui/log_viewer.cpp
        src/ui/login_dialog.cpp
        src/ui/user_manager.cpp
        src/ui/welcome_widget.cpp
    )

    set(STUDIO_HEADERS
        src/main_window.h
        src/network/goods_db_client.h
        src/network/database_worker.h
        src/network/protocol_text_adapter.h
        src/connection/connection_dialog.h
        src/connection/connection_pool.h
        src/sql_editor/sql_highlighter.h
        src/sql_editor/sql_editor_widget.h
        src/sql_editor/query_executor.h
        src/sql_editor/query_history.h
        src/result_view/result_table_model.h
        src/result_view/result_table_view.h
        src/result_view/result_pagination.h
        src/result_view/export_dialog.h
        src/object_tree/object_tree_model.h
        src/object_tree/object_tree_widget.h
        src/object_tree/table_info_panel.h
        src/ui/admin_panel.h
        src/ui/backup_wizard.h
        src/ui/log_viewer.h
        src/ui/login_dialog.h
        src/ui/user_manager.h
        src/ui/welcome_widget.h
    )

    set(STUDIO_RESOURCES resources/resources.qrc)

    add_executable(goods_db_studio ${STUDIO_SOURCES} ${STUDIO_HEADERS} ${STUDIO_RESOURCES})

    target_include_directories(goods_db_studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(goods_db_studio PRIVATE
        Qt6::Widgets
        Qt6::Sql
        Qt6::Network
    )

    message(STATUS "Qt6 found — building goods_db_studio desktop client")
else()
    message(STATUS "Qt6 not found — skipping desktop client, building web server only")
endif()
