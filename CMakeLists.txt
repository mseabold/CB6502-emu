cmake_minimum_required(VERSION 3.10.0)
project(CB6502emu)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TESTING "Enable the unit testing framework" OFF)

add_subdirectory(core)
add_subdirectory(plat/cb6502)
add_subdirectory(frontend)
add_subdirectory(io/sdcard)
add_subdirectory(io/at28c256)
add_subdirectory(dbginfo)

if(${ENABLE_TESTING})
    enable_testing()
    add_subdirectory(tests)
endif()
