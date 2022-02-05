add_library(cbemu STATIC
    src/cpu.c
    src/acia.c
    src/via.c)

target_include_directories(cbemu
    PUBLIC
        inc
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(cbemu PRIVATE Threads::Threads)
