add_library(dbginfo STATIC
    dbginfo.c
)

target_include_directories(dbginfo
    PUBLIC
        .
)
