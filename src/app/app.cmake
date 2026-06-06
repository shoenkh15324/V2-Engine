# cmake/app/app.cmake

message(STATUS "Configuring App Layer")

option(BUILD_DEMO_APP "Build demo app" ON)
option(BUILD_CLI_APP "Build cli app" ON)

add_library(v2_app STATIC)

target_sources(v2_app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/demo/demo_app.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cli/cli_app.cpp
)

target_include_directories(v2_app PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

target_compile_definitions(v2_app PRIVATE
    V2_ENGINE_NAME="${PROJECT_NAME}"
    V2_ENGINE_VERSION="${PROJECT_VERSION}"
)  

if(BUILD_DEMO_APP)
    add_executable(v2_demo
        ${CMAKE_CURRENT_LIST_DIR}/demo/main.cpp
    )
    target_link_libraries(v2_demo PRIVATE v2_app v2_service v2_core v2_infra)
endif()

if(BUILD_CLI_APP)
    add_executable(v2_cli
        ${CMAKE_CURRENT_LIST_DIR}/cli/main.cpp
    )
    target_link_libraries(v2_cli PRIVATE v2_app v2_service v2_core v2_infra)
endif()
