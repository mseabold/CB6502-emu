add_library(acia STATIC
    src/acia.c
    src/acia_console.c
)


if(UNIX)
    target_sources(acia PRIVATE src/acia_unix_sock.c)
endif()

target_include_directories(acia PUBLIC inc)
target_link_libraries(acia
    PUBLIC
        cbemu
)

