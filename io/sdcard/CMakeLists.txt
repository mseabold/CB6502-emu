add_library(sdcard STATIC src/sdcard.c)

target_include_directories(sdcard PUBLIC inc)
target_link_libraries(sdcard PUBLIC cbemu)
