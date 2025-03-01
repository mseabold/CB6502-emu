if(UNIX)
add_library(syslog_log STATIC
    src/syslog_log.c
)

target_include_directories(syslog_log
    PUBLIC
        inc
)

target_link_libraries(syslog_log
    PUBLIC
        log_iface
)

endif()

