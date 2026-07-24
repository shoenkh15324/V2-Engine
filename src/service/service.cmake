# cmake/service/service.cmake

message(STATUS "Configuring Service Layer")

add_library(v2_service STATIC
    ${CMAKE_CURRENT_LIST_DIR}/system/system_actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/monitor/monitor_actor.cpp    
    ${CMAKE_CURRENT_LIST_DIR}/tick/tick_actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device_manager/device_manager_actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cmd/cmd_actor.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(v2_service PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/ipc/ipc_server_actor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/dbus/dbus_actor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/dbus/dbus_server_handler.cpp
        ${CMAKE_CURRENT_LIST_DIR}/dbus/dbus_client_handler.cpp
        ${CMAKE_CURRENT_LIST_DIR}/network_manager/network_manager_actor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/network_manager/wifi_handler.cpp
    )
    target_link_libraries(v2_service PUBLIC SDBusCpp::sdbus-c++)
endif()

target_link_libraries(v2_service PUBLIC v2_core)

target_include_directories(v2_service PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
