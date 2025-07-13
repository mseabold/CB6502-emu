add_executable(bus_tester bus_tester.c)

target_include_directories(bus_tester
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src/priv
)

target_link_libraries(bus_tester
    unity::framework
    cbemu
    )


add_test(NAME bus_tester COMMAND bus_tester)

