add_library(memory STATIC src/memory.c)

target_include_directories(memory PUBLIC inc)
target_link_libraries(memory PUBLIC cbemu ioutil)
