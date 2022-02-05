add_executable(cbemu src/cpu.c src/cbemu.c src/acia.c src/via.c)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_include_directories(cbemu
    PUBLIC
        inc
)
target_link_libraries(cbemu PRIVATE Threads::Threads)
