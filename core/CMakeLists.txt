option(SUPPORT_65C02 "Support 65C02 opcodes" ON)

add_subdirectory(logging)
add_subdirectory(os)
add_subdirectory(util)

add_library(cbemu STATIC
    src/sys.c
    src/cpu.c
    src/debugger.c
)

target_include_directories(cbemu
    PUBLIC
        inc
)

target_link_libraries(cbemu
    PUBLIC
        dbginfo
        logging
        os_iface
        util
    PRIVATE
        os_port
)

if(SUPPORT_65C02)
    target_compile_definitions(cbemu PRIVATE SUPPORT_65C02)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(cbemu PRIVATE Threads::Threads)
