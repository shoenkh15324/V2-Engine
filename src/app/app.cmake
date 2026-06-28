# cmake/app/app.cmake

message(STATUS "Configuring App Layer")

option(BUILD_MAIN_APP "Build main app" ON)
option(BUILD_CLI_APP "Build cli app" ON)
option(BUILD_TUI_APP "Build tui app" ON)

if(BUILD_MAIN_APP)
    add_executable(v2_main
        ${CMAKE_CURRENT_LIST_DIR}/main/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/main/main_app.cpp
    )
    target_include_directories(v2_main PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../src
    )
    target_link_libraries(v2_main PRIVATE v2_service v2_core v2_infra)
    target_compile_definitions(v2_main PRIVATE
        V2_APP_VERSION="0.2.0"
    )
endif()

if(BUILD_CLI_APP)
    add_executable(v2_cli
        ${CMAKE_CURRENT_LIST_DIR}/cli/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/cli/cli_app.cpp
    )
    target_include_directories(v2_cli PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../src
    )
    target_link_libraries(v2_cli PRIVATE v2_service v2_core v2_infra)
    target_compile_definitions(v2_cli PRIVATE
        V2_APP_VERSION="0.0.2"
    )
endif()

if(BUILD_TUI_APP)
    add_executable(v2_tui
        ${CMAKE_CURRENT_LIST_DIR}/tui/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tui/tui_app.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tui/render_util.cpp
    )
    target_include_directories(v2_tui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../src
    )
    target_link_libraries(v2_tui PRIVATE v2_service v2_core v2_infra)
    target_compile_definitions(v2_tui PRIVATE
        V2_APP_VERSION="0.0.1"
    )
endif()
