# cmake/app/app.cmake

message(STATUS "Configuring App Layer")

option(BUILD_MAIN_APP "Build main app" ON)
option(BUILD_CLI_APP "Build cli app" ON)

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
        V2_ENGINE_NAME="${PROJECT_NAME}"
        V2_ENGINE_VERSION="${PROJECT_VERSION}"
        V2_APP_VERSION="0.0.5"
        V2_DEFAULT_WORKER_COUNT=4
        V2_MAINAPP_MAINLOOP_SLEEP_MS=100
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
        V2_APP_VERSION="0.0.1"
        V2_DEFAULT_WORKER_COUNT=2
        V2_ENABLE_TICK_ACTOR=0
    )
endif()
