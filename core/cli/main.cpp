#include <iomanip>
#include <iostream>

#include "../collectors/battery.hpp"
#include "../collectors/cpu.hpp"
#include "../collectors/memory.hpp"
#include "../collectors/process.hpp"

namespace {

double bytes_to_gb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

double bytes_to_mb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace

int main() {
    using namespace sysintel;

    std::cout << "System Intelligence -- status\n\n";

    std::cout << "Power\n";
    BatterySnapshot battery = get_battery_snapshot();
    if (battery.battery_present) {
        std::cout << "  Source          " << (battery.on_ac_power ? "AC" : "Battery") << "\n";
        std::cout << "  Charge          " << battery.charge_percent << "%\n";
        if (battery.rate_watts.has_value()) {
            double watts = *battery.rate_watts;
            std::cout << std::fixed << std::setprecision(1);
            if (watts < 0) {
                std::cout << "  Discharge       " << -watts << " W\n";
            } else if (watts > 0) {
                std::cout << "  Charge rate     " << watts << " W\n";
            } else {
                std::cout << "  Power draw      not reported\n";
            }
        }
    } else {
        std::cout << "  No battery detected (desktop system?)\n";
    }

    std::cout << "\nCPU\n";
    std::cout << "  (sampling for 1 second...)\r" << std::flush;
    CpuSnapshot cpu = get_cpu_snapshot();
    std::cout << "  Utilization     " << std::fixed << std::setprecision(1)
               << cpu.total_utilization_percent << "%        \n";

    std::cout << "\nMemory\n";
    MemorySnapshot mem = get_memory_snapshot();
    std::cout << "  Used            " << std::fixed << std::setprecision(1)
               << bytes_to_gb(mem.total_bytes - mem.available_bytes) << " / "
               << bytes_to_gb(mem.total_bytes) << " GB"
               << "  (" << mem.memory_load_percent << "%)\n";

    std::cout << "\nTop Processes (by memory)\n";
    auto processes = get_top_processes_by_memory(8);
    for (const auto& p : processes) {
        std::cout << "  " << std::left << std::setw(24) << p.name << std::right << std::fixed
                   << std::setprecision(1) << bytes_to_mb(p.working_set_bytes) << " MB\n";
    }

    std::cout << std::endl;
    return 0;
}
