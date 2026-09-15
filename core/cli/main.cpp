#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iomanip>
#include <iostream>
#include <string>

#include "../collectors/battery.hpp"
#include "../collectors/cpu.hpp"
#include "../collectors/memory.hpp"
#include "../collectors/process.hpp"
#include "../sampler/sampler.hpp"
#include "../storage/sqlite_store.hpp"

namespace {

using namespace sysintel;

double bytes_to_gb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

double bytes_to_mb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

int run_status() {
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

Sampler* g_sampler = nullptr;

BOOL WINAPI console_ctrl_handler(DWORD /*ctrl_type*/) {
    if (g_sampler) {
        g_sampler->request_stop();
    }
    return TRUE;
}

int run_record(const std::string& db_path) {
    std::cout << "Recording battery/CPU/memory samples to " << db_path
               << " (Ctrl+C to stop)...\n";

    SqliteStore store(db_path);
    Sampler sampler(store);
    g_sampler = &sampler;
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    sampler.run();

    std::cout << "\nStopped.\n";
    return 0;
}

int run_history(const std::string& db_path, const std::string& metric, int last_minutes) {
    SqliteStore store(db_path);

    int64_t until_ms = current_timestamp_ms();
    int64_t since_ms = until_ms - static_cast<int64_t>(last_minutes) * 60 * 1000;

    AggregatedValue agg = store.query_range(metric, since_ms, until_ms);

    if (agg.sample_count == 0) {
        std::cout << "No samples found for '" << metric << "' in the last " << last_minutes
                   << " minutes.\n";
        std::cout << "(Is 'sysintel record' running or has it run recently against this db?)\n";
        return 0;
    }

    std::cout << metric << " -- last " << last_minutes << " minutes (" << agg.sample_count
               << " samples)\n\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Min   " << agg.min_value << "\n";
    std::cout << "  Avg   " << agg.avg_value << "\n";
    std::cout << "  Max   " << agg.max_value << "\n";
    return 0;
}

void print_usage() {
    std::cerr << "usage:\n"
               << "  sysintel status\n"
               << "  sysintel record [--db <path>]\n"
               << "  sysintel history <metric> [--last <minutes>] [--db <path>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string command = argc > 1 ? argv[1] : "status";

    if (command == "status") {
        return run_status();
    }

    if (command == "record") {
        std::string db_path = "sysintel.db";
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            }
        }
        return run_record(db_path);
    }

    if (command == "history") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        std::string metric = argv[2];
        int last_minutes = 30;
        std::string db_path = "sysintel.db";
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--last" && i + 1 < argc) {
                last_minutes = std::stoi(argv[++i]);
            } else if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            }
        }
        return run_history(db_path, metric, last_minutes);
    }

    std::cerr << "unknown command: " << command << "\n";
    print_usage();
    return 1;
}
