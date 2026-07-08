function(add_v2_test name source)
    add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/${source})
    target_link_libraries(${name} PRIVATE v2_core GTest::gtest_main GTest::gmock)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    gtest_discover_tests(${name})
endfunction()

add_v2_test(test_actor_system_integration core/test_actor_system_integration.cpp)
