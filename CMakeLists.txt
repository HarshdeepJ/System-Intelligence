cmake_minimum_required(VERSION 3.20)
project(SystemIntelligence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sysintel
    core/collectors/battery.cpp
    core/collectors/cpu.cpp
    core/collectors/memory.cpp
    core/collectors/process.cpp
    core/cli/main.cpp
)

target_link_libraries(sysintel PRIVATE pdh powrprof psapi)
