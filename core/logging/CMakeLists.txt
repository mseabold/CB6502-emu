add_library(log_iface INTERFACE)

target_include_directories(log_iface
    INTERFACE
        inc
)

add_library(logging STATIC)

target_sources(logging
    PRIVATE
        src/log.c
        src/console_log.c
)

target_link_libraries(logging
    PUBLIC
        log_iface
)



