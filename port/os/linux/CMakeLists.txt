add_library(os_port STATIC)

target_sources(os_port
    PRIVATE
        src/os_signal.c
)

target_include_directories(os_port
    PUBLIC
        inc
)

#WAR until refactoring is complete
target_include_directories(os_port
    PUBLIC
        ../../../core/inc
)
