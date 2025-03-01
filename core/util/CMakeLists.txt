# For now, make this INTERFACE as there is no associated code
add_library(util INTERFACE)

target_include_directories(util
    INTERFACE
        inc
)
