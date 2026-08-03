# cmake/test/standalone/standalone_test.cmake
# v2_core_smoke — core가 infra/nlohmann/ftxui 없이 단독 빌드·실행되는지 증명.
# v2_core만 링크하므로 core에 외부 의존이 스며들면 컴파일/링크 에러로 검출된다.

add_executable(v2_core_smoke
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)
target_link_libraries(v2_core_smoke PRIVATE v2_core)
target_include_directories(v2_core_smoke PRIVATE ${CMAKE_CURRENT_LIST_DIR})

add_test(NAME Core.SmokeTest COMMAND v2_core_smoke)
