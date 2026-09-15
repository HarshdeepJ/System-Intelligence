#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "../anomalies/battery_detector.hpp"
#include "../collectors/battery.hpp"
#include "../collectors/cpu.hpp"
#include "../collectors/memory.hpp"
#include "../collectors/process.hpp"
#include "../actions/action_broker.hpp"
#include "../actions/power_scheme.hpp"
#include "../model/gpu_state.hpp"
#include "../model/system_inventory.hpp"
#include "../sampler/sampler.hpp"
#include "../storage/sqlite_store.hpp"
#include "../util/json.hpp"

namespace {

using namespace sysintel;

double bytes_to_gb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

double bytes_to_mb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// JSON encoding of a Reading<T>: {"value": ... | null, "availability": "..."}
// -- this is how Python's side ever gets to see the ok/unsupported/
// unavailable/error distinction, not just a bare value.
std::string json_reading_value(const std::string& v) { return "\"" + json_escape(v) + "\""; }
std::string json_reading_value(int v) { return std::to_string(v); }
std::string json_reading_value(uint64_t v) { return std::to_string(v); }
std::string json_reading_value(double v) { return json_num(v); }

template <typename T>
std::string json_reading(const Reading<T>& r) {
    std::ostringstream out;
    out << "{\"value\":" << (r.value.has_value() ? json_reading_value(*r.value) : "null")
        << ",\"availability\":\"" << availability_name(r.availability) << "\"}";
    return out.str();
}

std::string json_gpu_array(const std::vector<GpuState>& gpus) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < gpus.size(); ++i) {
        if (i > 0) out << ",";
        const auto& g = gpus[i];
        out << "{\"vendor\":\"" << json_escape(g.vendor) << "\",\"model\":\""
            << json_escape(g.model) << "\""
            << ",\"utilization_percent\":" << json_reading(g.utilization_percent)
            << ",\"used_vram_bytes\":" << json_reading(g.used_vram_bytes)
            << ",\"total_vram_bytes\":" << json_reading(g.total_vram_bytes)
            << ",\"temperature_celsius\":" << json_reading(g.temperature_celsius)
            << ",\"power_watts\":" << json_reading(g.power_watts)
            << ",\"performance_state\":" << json_reading(g.performance_state) << "}";
    }
    out << "]";
    return out.str();
}

int run_status_json() {
    BatterySnapshot battery = get_battery_snapshot();
    CpuSnapshot cpu = get_cpu_snapshot();
    MemorySnapshot mem = get_memory_snapshot();
    auto processes = get_top_processes_by_memory(8);
    auto gpus = collect_gpu_state();

    std::ostringstream out;
    out << "{";

    out << "\"battery\":";
    if (battery.battery_present) {
        out << "{\"present\":true"
            << ",\"on_ac_power\":" << (battery.on_ac_power ? "true" : "false")
            << ",\"charging\":" << (battery.charging ? "true" : "false")
            << ",\"charge_percent\":" << battery.charge_percent << ",\"rate_watts\":"
            << (battery.rate_watts.has_value() ? json_num(*battery.rate_watts) : "null") << "}";
    } else {
        out << "{\"present\":false}";
    }

    out << ",\"cpu\":{\"utilization_percent\":" << json_num(cpu.total_utilization_percent) << "}";

    out << ",\"memory\":{\"total_bytes\":" << mem.total_bytes
        << ",\"available_bytes\":" << mem.available_bytes
        << ",\"load_percent\":" << mem.memory_load_percent << "}";

    out << ",\"gpu\":" << json_gpu_array(gpus);

    out << ",\"top_processes_by_memory\":[";
    for (size_t i = 0; i < processes.size(); ++i) {
        if (i > 0) out << ",";
        out << "{\"pid\":" << processes[i].pid << ",\"name\":\""
            << json_escape(processes[i].name) << "\",\"working_set_bytes\":"
            << processes[i].working_set_bytes << "}";
    }
    out << "]";

    out << "}";
    std::cout << out.str() << std::endl;
    return 0;
}

template <typename T>
void print_reading(const std::string& label, const Reading<T>& r) {
    std::cout << "  " << std::left << std::setw(16) << label;
    if (r.value.has_value()) {
        std::cout << *r.value;
    } else {
        std::cout << availability_name(r.availability);
    }
    std::cout << "\n";
}

void print_event(const SystemEvent& event);  // defined further below, used here for --db output

int run_power_schemes() {
    auto schemes = enumerate_power_schemes();
    auto active = get_active_power_scheme();

    std::cout << "Power schemes on this machine:\n\n";
    if (schemes.empty()) {
        std::cout << "  (none enumerated -- PowerEnumerate failed or returned nothing)\n";
    }
    for (const auto& s : schemes) {
        bool is_active = active.has_value() && s.guid_string == active->guid_string;
        std::cout << "  " << (is_active ? "* " : "  ") << s.friendly_name << "  [" << s.guid_string
                   << "]" << (is_active ? "  (active)" : "") << "\n";
    }

    std::cout << "\nPower Mode slider (on battery / DC):\n";
    std::string dc_guid = get_dc_power_mode_raw_guid();
    if (dc_guid == guid_to_string(power_mode_guid(PowerModeLevel::kBestPowerEfficiency))) {
        std::cout << "  " << power_mode_name(PowerModeLevel::kBestPowerEfficiency) << "  [" << dc_guid
                   << "]\n";
    } else if (dc_guid == guid_to_string(power_mode_guid(PowerModeLevel::kBestPerformance))) {
        std::cout << "  " << power_mode_name(PowerModeLevel::kBestPerformance) << "  [" << dc_guid
                   << "]\n";
    } else {
        std::cout << "  unrecognized/balanced  [" << dc_guid << "]\n";
    }

    std::cout << "\nOverlay schemes Windows itself reports (debug):\n";
    for (const auto& s : debug_enumerate_overlay_schemes()) {
        std::cout << "  " << s.friendly_name << "  [" << s.guid_string << "]\n";
    }
    return 0;
}

int run_inspect(const std::string& db_path) {
    SystemInventory inv = collect_system_inventory();

    std::cout << "====== SYSTEM INTELLIGENCE: inspect ======\n\n";

    std::cout << "SYSTEM\n";
    print_reading("Manufacturer", inv.manufacturer);
    print_reading("Model", inv.model);
    print_reading("OS", inv.os.caption);
    print_reading("OS Version", inv.os.version);
    print_reading("Build", inv.os.build_number);

    std::cout << "\nCPU\n";
    print_reading("Model", inv.cpu.model);
    print_reading("Cores", inv.cpu.physical_cores);
    print_reading("Threads", inv.cpu.logical_processors);

    std::cout << "\nGPU\n";
    if (inv.gpus.empty()) {
        std::cout << "  (none detected)\n";
    }
    for (size_t i = 0; i < inv.gpus.size(); ++i) {
        const auto& gpu = inv.gpus[i];
        std::cout << "  [" << i << "] " << gpu.vendor << " -- " << gpu.model << "\n";
        if (gpu.vram_bytes.value.has_value()) {
            std::cout << "      VRAM            " << bytes_to_mb(*gpu.vram_bytes.value)
                       << " MB\n";
        } else {
            std::cout << "      VRAM            " << availability_name(gpu.vram_bytes.availability)
                       << "\n";
        }
    }

    std::cout << "\nGPU STATE (live)\n";
    auto gpu_states = collect_gpu_state();
    if (gpu_states.empty()) {
        std::cout << "  (none detected)\n";
    }
    for (size_t i = 0; i < gpu_states.size(); ++i) {
        const auto& gpu = gpu_states[i];
        std::cout << "  [" << i << "] " << gpu.vendor << " -- " << gpu.model << "\n";
        print_reading("    Utilization", gpu.utilization_percent);
        if (gpu.used_vram_bytes.value.has_value() && gpu.total_vram_bytes.value.has_value()) {
            std::cout << "    VRAM            " << bytes_to_mb(*gpu.used_vram_bytes.value) << " / "
                       << bytes_to_mb(*gpu.total_vram_bytes.value) << " MB\n";
        } else {
            std::cout << "    VRAM            "
                       << availability_name(gpu.used_vram_bytes.availability) << "\n";
        }
        print_reading("    Temperature", gpu.temperature_celsius);
        print_reading("    Power (W)", gpu.power_watts);
        print_reading("    P-State", gpu.performance_state);
    }

    std::cout << "\nMEMORY\n";
    print_reading("DIMMs", inv.memory.dimm_count);
    if (inv.memory.total_capacity_bytes.value.has_value()) {
        std::cout << "  " << std::left << std::setw(16) << "Total"
                   << bytes_to_gb(*inv.memory.total_capacity_bytes.value) << " GB\n";
    } else {
        print_reading("Total", inv.memory.total_capacity_bytes);
    }

    std::cout << "\nDISKS\n";
    if (inv.disks.empty()) {
        std::cout << "  (none detected)\n";
    }
    for (size_t i = 0; i < inv.disks.size(); ++i) {
        const auto& disk = inv.disks[i];
        std::cout << "  [" << i << "] " << disk.model << "\n";
        if (disk.size_bytes.value.has_value()) {
            std::cout << "      Size            " << bytes_to_gb(*disk.size_bytes.value)
                       << " GB\n";
        } else {
            std::cout << "      Size            " << availability_name(disk.size_bytes.availability)
                       << "\n";
        }
    }

    std::cout << "\nBATTERY\n";
    print_reading("Chemistry", inv.battery.chemistry);
    print_reading("Design cap.", inv.battery.design_capacity_mwh);
    print_reading("Full charge", inv.battery.full_charge_capacity_mwh);

    if (!db_path.empty()) {
        std::cout << "\nRECENT EVENTS\n";
        SqliteStore store(db_path);
        int64_t since_ms = current_timestamp_ms() - 30LL * 60 * 1000;
        auto events = store.query_recent_events(since_ms, 10);
        if (events.empty()) {
            std::cout << "  (none in the last 30 minutes -- is 'sysintel record' or 'watch' "
                          "running against this db?)\n";
        } else {
            for (const auto& event : events) {
                print_event(event);
            }
        }
    }

    std::cout << "\n===========================================\n";
    return 0;
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

int run_history(const std::string& db_path, const std::string& metric, int last_minutes,
                bool as_json) {
    SqliteStore store(db_path);

    int64_t until_ms = current_timestamp_ms();
    int64_t since_ms = until_ms - static_cast<int64_t>(last_minutes) * 60 * 1000;

    AggregatedValue agg = store.query_range(metric, since_ms, until_ms);

    if (as_json) {
        std::cout << "{\"metric\":\"" << json_escape(metric) << "\",\"last_minutes\":"
                   << last_minutes << ",\"count\":" << agg.sample_count
                   << ",\"min\":" << (agg.sample_count > 0 ? json_num(agg.min_value) : "null")
                   << ",\"avg\":" << (agg.sample_count > 0 ? json_num(agg.avg_value) : "null")
                   << ",\"max\":" << (agg.sample_count > 0 ? json_num(agg.max_value) : "null")
                   << "}" << std::endl;
        return 0;
    }

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

void print_event(const SystemEvent& event) {
    std::cout << "  " << event.timestamp_ms << "  " << event.type;
    for (const auto& [key, value] : event.data) {
        std::cout << "  " << key << "=" << value;
    }
    std::cout << "\n";
}

std::string json_events_array(const std::vector<SystemEvent>& events) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) out << ",";
        const auto& e = events[i];
        out << "{\"timestamp_ms\":" << e.timestamp_ms << ",\"type\":\"" << json_escape(e.type)
            << "\",\"data\":{";
        bool first = true;
        for (const auto& [key, value] : e.data) {
            if (!first) out << ",";
            first = false;
            out << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
        }
        out << "}}";
    }
    out << "]";
    return out.str();
}

int run_events(const std::string& db_path, int last_minutes, int limit, bool as_json) {
    SqliteStore store(db_path);
    int64_t since_ms = current_timestamp_ms() - static_cast<int64_t>(last_minutes) * 60 * 1000;
    auto events = store.query_recent_events(since_ms, limit);

    if (as_json) {
        std::cout << "{\"last_minutes\":" << last_minutes
                   << ",\"events\":" << json_events_array(events) << "}" << std::endl;
        return 0;
    }

    if (events.empty()) {
        std::cout << "No events in the last " << last_minutes << " minutes.\n";
        return 0;
    }

    std::cout << events.size() << " event(s) in the last " << last_minutes << " minutes:\n\n";
    for (const auto& event : events) {
        print_event(event);
    }
    return 0;
}

void print_action_outcome(const ActionOutcome& outcome) {
    if (!outcome.known_action) {
        std::cout << "Refused: " << outcome.message << "\n";
        return;
    }
    if (!outcome.executed) {
        std::cout << (outcome.success ? "No-op: " : "Preview (not applied): ") << outcome.message
                   << "\n";
        if (!outcome.previous_state.empty()) {
            std::cout << "  Current state    " << outcome.previous_state << "\n";
            std::cout << "  Would become     " << outcome.new_state << "\n";
        }
        return;
    }
    std::cout << (outcome.success ? "Applied: " : "FAILED: ") << outcome.message << "\n";
    std::cout << "  Previous state   " << outcome.previous_state << "\n";
    std::cout << "  New state        " << outcome.new_state << "\n";
    if (!outcome.action_id.empty()) {
        std::cout << "  Action id        " << outcome.action_id
                   << "  (use 'sysintel act rollback " << outcome.action_id << "' to undo)\n";
    }
}

std::string json_action_outcome(const ActionOutcome& outcome) {
    std::ostringstream out;
    out << "{\"known_action\":" << (outcome.known_action ? "true" : "false")
        << ",\"approved\":" << (outcome.approved ? "true" : "false")
        << ",\"executed\":" << (outcome.executed ? "true" : "false")
        << ",\"success\":" << (outcome.success ? "true" : "false") << ",\"previous_state\":\""
        << json_escape(outcome.previous_state) << "\",\"new_state\":\""
        << json_escape(outcome.new_state) << "\",\"message\":\"" << json_escape(outcome.message)
        << "\",\"action_id\":\"" << json_escape(outcome.action_id) << "\"}";
    return out.str();
}

int run_act_change_power_mode(const std::string& db_path, const std::string& level,
                               const std::string& reason, bool approved, bool as_json) {
    SqliteStore store(db_path);
    ActionBroker broker(store);

    ActionRequest request;
    request.action_type = "change_power_mode";
    request.reason = reason;
    request.params["level"] = level;

    ActionOutcome outcome = broker.execute(request, approved);
    if (as_json) {
        std::cout << json_action_outcome(outcome) << std::endl;
    } else {
        print_action_outcome(outcome);
    }
    return 0;
}

int run_act_rollback(const std::string& db_path, const std::string& action_id, bool approved,
                      bool as_json) {
    SqliteStore store(db_path);
    ActionBroker broker(store);
    ActionOutcome outcome = broker.rollback(action_id, approved);
    if (as_json) {
        std::cout << json_action_outcome(outcome) << std::endl;
    } else {
        print_action_outcome(outcome);
    }
    return 0;
}

int run_actions(const std::string& db_path, int limit) {
    SqliteStore store(db_path);
    auto actions = store.query_recent_actions(limit);

    if (actions.empty()) {
        std::cout << "No actions recorded yet.\n";
        return 0;
    }

    std::cout << actions.size() << " most recent action(s):\n\n";
    for (const auto& a : actions) {
        std::cout << "  " << a.id << "  [" << a.action_type << "]"
                   << (a.success ? "" : " FAILED") << (a.rolled_back ? " (rolled back)" : "")
                   << "\n"
                   << "    reason: " << a.reason << "\n"
                   << "    " << a.previous_state << " -> " << a.new_state << "\n";
    }
    return 0;
}

void print_check_report(const BatteryCheckReport& report) {
    switch (report.result) {
        case BatteryCheckResult::kNotEnoughHistory:
            std::cout << "Not enough history yet to evaluate battery anomalies.\n";
            break;
        case BatteryCheckResult::kNoRecentDischarge:
            std::cout << "No recent discharge samples (on AC, or not run long enough) -- "
                          "skipping check.\n";
            break;
        case BatteryCheckResult::kNormal:
            std::cout << "Battery normal. Baseline " << std::fixed << std::setprecision(1)
                       << report.baseline_mean << "W (+/-" << report.baseline_stddev << "W)\n";
            break;
        case BatteryCheckResult::kAnomalyOpened:
            std::cout << "ANOMALY DETECTED: " << report.incident->id << "\n"
                       << "  Observed  " << std::fixed << std::setprecision(1) << report.live_mean
                       << " W\n"
                       << "  Baseline  " << report.baseline_mean << " W\n"
                       << "  Threshold " << report.threshold << " W\n";
            break;
        case BatteryCheckResult::kAnomalyOngoing:
            std::cout << "Anomaly ongoing: " << report.incident->id << " -- " << std::fixed
                       << std::setprecision(1) << report.live_mean << " W\n";
            break;
        case BatteryCheckResult::kResolved:
            std::cout << "Resolved: " << report.incident->id << " -- back to " << std::fixed
                       << std::setprecision(1) << report.live_mean << " W\n";
            break;
    }
}

const char* check_result_name(BatteryCheckResult result) {
    switch (result) {
        case BatteryCheckResult::kNotEnoughHistory:
            return "not_enough_history";
        case BatteryCheckResult::kNoRecentDischarge:
            return "no_recent_discharge";
        case BatteryCheckResult::kNormal:
            return "normal";
        case BatteryCheckResult::kAnomalyOpened:
            return "anomaly_opened";
        case BatteryCheckResult::kAnomalyOngoing:
            return "anomaly_ongoing";
        case BatteryCheckResult::kResolved:
            return "resolved";
    }
    return "unknown";
}

void print_check_report_json(const BatteryCheckReport& report) {
    std::ostringstream out;
    out << "{\"result\":\"" << check_result_name(report.result) << "\""
        << ",\"live_mean_watts\":" << json_num(report.live_mean)
        << ",\"baseline_mean_watts\":" << json_num(report.baseline_mean)
        << ",\"baseline_stddev_watts\":" << json_num(report.baseline_stddev)
        << ",\"threshold_watts\":" << json_num(report.threshold) << ",\"incident\":";

    if (report.incident.has_value()) {
        const Incident& inc = *report.incident;
        out << "{\"id\":\"" << json_escape(inc.id) << "\",\"domain\":\"" << json_escape(inc.domain)
            << "\",\"status\":\"" << json_escape(inc.status) << "\",\"severity\":\""
            << json_escape(inc.severity) << "\",\"started_at_ms\":" << inc.started_at_ms
            << ",\"resolved_at_ms\":" << inc.resolved_at_ms << ",\"trigger_type\":\""
            << json_escape(inc.trigger_type) << "\",\"observed_value\":"
            << json_num(inc.observed_value)
            << ",\"baseline_mean\":" << json_num(inc.baseline_mean) << "}";
    } else {
        out << "null";
    }

    out << "}";
    std::cout << out.str() << std::endl;
}

int run_check_battery(const std::string& db_path, int min_history_days, bool as_json) {
    SqliteStore store(db_path);
    BatteryAnomalyConfig config;
    config.min_history_days = min_history_days;
    BatteryAnomalyDetector detector(store, config);

    BatteryCheckReport report = detector.check();
    if (as_json) {
        print_check_report_json(report);
    } else {
        print_check_report(report);
    }
    return 0;
}

int run_watch(const std::string& db_path, int min_history_days) {
    std::cout << "Watching battery/CPU/memory (recording + anomaly checks) -- Ctrl+C to stop\n";
    std::cout << "Anomaly baseline requires " << min_history_days
               << " day(s) of accumulated history.\n\n";

    SqliteStore store(db_path);
    Sampler sampler(store);
    g_sampler = &sampler;
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    BatteryAnomalyConfig config;
    config.min_history_days = min_history_days;
    BatteryAnomalyDetector detector(store, config);

    sampler.set_periodic_hook(std::chrono::seconds(60), [&detector]() {
        std::cout << "[watch] ";
        print_check_report(detector.check());
    });

    sampler.run();

    std::cout << "\nStopped.\n";
    return 0;
}

void print_usage() {
    std::cerr << "usage:\n"
               << "  sysintel status [--json]\n"
               << "  sysintel inspect [--db <path>]\n"
               << "  sysintel record [--db <path>]\n"
               << "  sysintel watch [--db <path>] [--min-history-days <n>]\n"
               << "  sysintel history <metric> [--last <minutes>] [--db <path>] [--json]\n"
               << "  sysintel events [--last <minutes>] [--limit <n>] [--db <path>] [--json]\n"
               << "  sysintel act change-power-mode --level <best_power_efficiency|best_performance> "
                  "[--reason <text>] [--db <path>] [--yes]\n"
               << "  sysintel act rollback <action-id> [--db <path>] [--yes]\n"
               << "  sysintel actions [--last <n>] [--db <path>]\n"
               << "  sysintel check-battery [--db <path>] [--min-history-days <n>] [--json]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string command = argc > 1 ? argv[1] : "status";

    if (command == "status") {
        bool as_json = argc > 2 && std::string(argv[2]) == "--json";
        return as_json ? run_status_json() : run_status();
    }

    if (command == "power-schemes") {
        return run_power_schemes();
    }

    if (command == "inspect") {
        std::string db_path;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            }
        }
        return run_inspect(db_path);
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
        bool as_json = false;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--last" && i + 1 < argc) {
                last_minutes = std::stoi(argv[++i]);
            } else if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            } else if (arg == "--json") {
                as_json = true;
            }
        }
        return run_history(db_path, metric, last_minutes, as_json);
    }

    if (command == "events") {
        std::string db_path = "sysintel.db";
        int last_minutes = 60;
        int limit = 50;
        bool as_json = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--last" && i + 1 < argc) {
                last_minutes = std::stoi(argv[++i]);
            } else if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            } else if (arg == "--limit" && i + 1 < argc) {
                limit = std::stoi(argv[++i]);
            } else if (arg == "--json") {
                as_json = true;
            }
        }
        return run_events(db_path, last_minutes, limit, as_json);
    }

    if (command == "act") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        std::string subcommand = argv[2];
        std::string db_path = "sysintel.db";
        bool approved = false;

        if (subcommand == "change-power-mode") {
            std::string level;
            std::string reason = "manual";
            bool as_json = false;
            for (int i = 3; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--level" && i + 1 < argc) {
                    level = argv[++i];
                } else if (arg == "--reason" && i + 1 < argc) {
                    reason = argv[++i];
                } else if (arg == "--db" && i + 1 < argc) {
                    db_path = argv[++i];
                } else if (arg == "--yes") {
                    approved = true;
                } else if (arg == "--json") {
                    as_json = true;
                }
            }
            if (level.empty()) {
                std::cerr << "usage: sysintel act change-power-mode --level "
                             "<best_power_efficiency|best_performance> [--reason <text>] "
                             "[--db <path>] [--yes] [--json]\n";
                return 1;
            }
            return run_act_change_power_mode(db_path, level, reason, approved, as_json);
        }

        if (subcommand == "rollback") {
            if (argc < 4) {
                std::cerr
                    << "usage: sysintel act rollback <action-id> [--db <path>] [--yes] [--json]\n";
                return 1;
            }
            std::string action_id = argv[3];
            bool as_json = false;
            for (int i = 4; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--db" && i + 1 < argc) {
                    db_path = argv[++i];
                } else if (arg == "--yes") {
                    approved = true;
                } else if (arg == "--json") {
                    as_json = true;
                }
            }
            return run_act_rollback(db_path, action_id, approved, as_json);
        }

        std::cerr << "unknown 'act' subcommand: " << subcommand << "\n";
        print_usage();
        return 1;
    }

    if (command == "actions") {
        std::string db_path = "sysintel.db";
        int limit = 20;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--last" && i + 1 < argc) {
                limit = std::stoi(argv[++i]);
            } else if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            }
        }
        return run_actions(db_path, limit);
    }

    if (command == "watch" || command == "check-battery") {
        std::string db_path = "sysintel.db";
        int min_history_days = 14;
        bool as_json = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            } else if (arg == "--min-history-days" && i + 1 < argc) {
                min_history_days = std::stoi(argv[++i]);
            } else if (arg == "--json") {
                as_json = true;
            }
        }
        return command == "watch" ? run_watch(db_path, min_history_days)
                                   : run_check_battery(db_path, min_history_days, as_json);
    }

    std::cerr << "unknown command: " << command << "\n";
    print_usage();
    return 1;
}
