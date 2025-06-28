cmake_minimum_required(VERSION 3.14)
project(pulse LANGUAGES CXX)

# Build the Pulse static library
add_library(pulse STATIC
    pulse.cpp
)

# Public include directory for clients
target_include_directories(pulse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Compiler warnings
target_compile_options(pulse PRIVATE -Wall -Wextra -pedantic)

# Link pthread for std::thread support & JSON
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

target_link_libraries(pulse
    PUBLIC
        Threads::Threads
        nlohmann_json::nlohmann_json
)
