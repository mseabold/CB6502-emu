#add_subdirectory(acia)
#add_subdirectory(at28c256)
#add_subdirectory(sdcard)
#add_subdirectory(via)
add_subdirectory(memory)

add_library(ioutil INTERFACE)
target_include_directories(ioutil INTERFACE inc)
target_link_libraries(ioutil INTERFACE cbemu)
