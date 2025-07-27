add_executable(bus_tester bus_tester.c)
add_executable(clock_tester clock_tester.c)
add_executable(cpu_unit_tester cpu_unit_tester.c)

add_library(cbemu_priv INTERFACE)

target_include_directories(cbemu_priv
    INTERFACE
        ${CMAKE_SOURCE_DIR}/core/src/priv
)

target_link_libraries(bus_tester
    unity::framework
    cbemu
    cbemu_priv
)

target_link_libraries(clock_tester
    unity::framework
    cbemu
    cbemu_priv
)

target_link_libraries(cpu_unit_tester
    unity::framework
    cbemu
    cbemu_priv
)

add_test(NAME bus_tester COMMAND bus_tester)
add_test(NAME clock_tester COMMAND clock_tester)
add_test(NAME cpu_unit_tester COMMAND cpu_unit_tester)

