add_library(at28c256 STATIC src/at28c256.c)

target_include_directories(at28c256 PUBLIC inc)
target_link_libraries(at28c256 PUBLIC cbemu)
