if(UNIX)
add_library(syslog_log STATIC
    src/syslog_log.c
)

target_include_directories(syslog_log
    PUBLIC
        inc
)

#WAR for including log.h until full refactoring is done
target_include_directories(syslog_log
    PUBLIC
        ../../../core/inc
)

endif()

