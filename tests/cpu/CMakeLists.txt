add_executable(cpu_tester cpu_tester.c)

target_link_libraries(cpu_tester
    unity::framework
    cbemu
    )


add_test(NAME cpu_tester COMMAND cpu_tester)
