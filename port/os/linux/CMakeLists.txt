add_library(os_port STATIC)

target_sources(os_port
    PRIVATE
        src/os_signal.c
)

target_include_directories(os_port
    PUBLIC
        inc
)

target_link_libraries(os_port
    PUBLIC
        os_iface
    PRIVATE
        util
)
