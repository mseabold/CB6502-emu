option(SUPPORT_65C02 "Support 65C02 opcodes" ON)

add_library(cbemu STATIC
    src/cpu.c
    src/acia.c
    src/via.c
    src/debugger.c)

target_include_directories(cbemu
    PUBLIC
        inc
)

if(SUPPORT_65C02)
    target_compile_definitions(cbemu PRIVATE SUPPORT_65C02)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(cbemu PRIVATE Threads::Threads)
