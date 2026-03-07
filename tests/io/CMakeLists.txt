if(0)
add_executable(at28c256_test
    at28c256_tester.c)

target_link_libraries(at28c256_test
    unity::framework
    cbemu
    at28c256
    )

add_test(NAME at28c256_test COMMAND at28c256_test)
endif()

add_executable(memory_tester memory_tester.c)

target_link_libraries(memory_tester
    unity::framework
    cbemu
    memory)

target_include_directories(memory_tester
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src/priv
)

add_test(NAME memory_tester COMMAND memory_tester)

add_executable(via_tester via_tester.c)

target_link_libraries(via_tester
    unity::framework
    cbemu
    via)

target_include_directories(via_tester
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src/priv
)

add_test(NAME via_tester COMMAND via_tester)
