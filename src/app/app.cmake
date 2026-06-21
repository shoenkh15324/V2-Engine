# cmake/app/app.cmake

message(STATUS "Configuring App Layer")

option(BUILD_DEMO_APP "Build demo app" ON)
option(BUILD_CLI_APP "Build cli app" ON)

if(BUILD_DEMO_APP)
    add_executable(v2_demo
        ${CMAKE_CURRENT_LIST_DIR}/demo/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/demo/demo_app.cpp
    )
    target_include_directories(v2_demo PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../src
    )
    target_link_libraries(v2_demo PRIVATE v2_service v2_core v2_infra)
    target_compile_definitions(v2_demo PRIVATE
        V2_ENGINE_NAME="${PROJECT_NAME}"
        V2_ENGINE_VERSION="${PROJECT_VERSION}"
        V2_APP_VERSION="0.0.5"
        V2_DEFAULT_WORKER_COUNT=4
        V2_DEMO_MAINLOOP_SLEEP_MS=100
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
        V2_ENABLE_TICK=0
    )
endif()
