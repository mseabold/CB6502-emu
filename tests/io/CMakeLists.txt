add_executable(at28c256_test
    at28c256_tester.c)

target_link_libraries(at28c256_test
    unity::framework
    cbemu
    at28c256
    )

add_test(NAME at28c256_test COMMAND at28c256_test)

