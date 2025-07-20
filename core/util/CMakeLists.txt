# For now, make this INTERFACE as there is no associated code
add_library(util src/util.c)

target_include_directories(util
    PUBLIC
        inc
)
