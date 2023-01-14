option(SUPPORT_65C02 "Support 65C02 opcodes" ON)

add_library(cbemu STATIC
    src/sys.c
    src/cpu.c
    src/acia.c
    src/via.c
    src/debugger.c
    src/log.c
    src/acia_console.c
)

if(UNIX)
    target_sources(cbemu PRIVATE src/acia_unix_sock.c)
endif()

target_include_directories(cbemu
    PUBLIC
        inc
)

target_link_libraries(cbemu
    PUBLIC
        dbginfo
)

if(SUPPORT_65C02)
    target_compile_definitions(cbemu PRIVATE SUPPORT_65C02)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(cbemu PRIVATE Threads::Threads)

add_library(console_log STATIC
    src/console_log.c
)

target_include_directories(console_log
    PUBLIC
        inc
)

add_library(syslog_log STATIC
    src/syslog_log.c
)

target_include_directories(syslog_log
    PUBLIC
        inc
)
