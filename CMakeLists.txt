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
    core/anomalies/anomaly_detector.cpp
    core/anomalies/battery_detector.cpp
    core/providers/windows/wmi_client.cpp
    core/providers/nvidia/nvml_provider.cpp
    core/inventory/system_inventory.cpp
    core/events/event_detector.cpp
    core/collectors/gpu.cpp
    core/actions/power_scheme.cpp
    core/actions/action_broker.cpp
    core/actions/process_control.cpp
    core/collectors/network.cpp
    core/collectors/disk_io.cpp
    core/collectors/thermal.cpp
    core/cli/main.cpp
    third_party/sqlite/sqlite3.c
)

target_include_directories(sysintel PRIVATE third_party/sqlite)

target_link_libraries(sysintel PRIVATE pdh powrprof psapi)
