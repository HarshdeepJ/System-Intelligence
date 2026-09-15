cmake_minimum_required(VERSION 3.20)
project(SystemIntelligence LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sysintel
    core/collectors/battery.cpp
    core/collectors/cpu.cpp
    core/collectors/memory.cpp
    core/collectors/process.cpp
    core/storage/sqlite_store.cpp
    core/sampler/sampler.cpp
    core/anomalies/battery_detector.cpp
    core/cli/main.cpp
    third_party/sqlite/sqlite3.c
)

target_include_directories(sysintel PRIVATE third_party/sqlite)

target_link_libraries(sysintel PRIVATE pdh powrprof psapi)
