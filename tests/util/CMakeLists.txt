add_executable(list_tester list_tester.c)

target_link_libraries(list_tester
    unity::framework
    cbemu
)

add_test(NAME list_test COMMAND list_tester)
