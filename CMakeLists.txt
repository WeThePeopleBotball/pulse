cmake_minimum_required(VERSION 3.14)
project(pulse LANGUAGES CXX)

# Build the Pulse static library
add_library(pulse STATIC
    pulse.cpp
)

target_include_directories(pulse PUBLIC ${JSON_LIB})

# Compiler warnings
target_compile_options(pulse PRIVATE -Wall -Wextra -pedantic)

# Link pthread for std::thread support & JSON
find_package(Threads REQUIRED)

target_link_libraries(pulse
    PUBLIC
        Threads::Threads
)
