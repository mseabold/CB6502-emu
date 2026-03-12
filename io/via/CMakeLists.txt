add_library(via STATIC src/via.c)

target_include_directories(via PUBLIC inc)
target_link_libraries(via PUBLIC cbemu)


