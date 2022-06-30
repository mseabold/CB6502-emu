add_library(dbgcli STATIC src/dbgcli.c)

target_include_directories(dbgcli
    PUBLIC
        inc
)

target_link_libraries(dbgcli
    PUBLIC
        cbemu
)
