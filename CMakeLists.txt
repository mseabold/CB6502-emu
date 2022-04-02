cmake_minimum_required(VERSION 3.1.0)
project(CB6502emu)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(core)
add_subdirectory(plat/cb6502)
add_subdirectory(io/sdcard)
