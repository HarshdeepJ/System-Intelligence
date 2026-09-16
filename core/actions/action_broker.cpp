#include "action_broker.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

#include "../collectors/process.hpp"
#include "power_scheme.hpp"
#include "process_control.hpp"

namespace sysintel {

namespace {

// Processes this broker will never suspend, regardless of what a diagnosis
// recommends -- suspending any of these can hang the desktop or the OS
// itself, and "resumable in principle" isn't the same as "safe to actually
// try." Matched case-insensitively against the process name only (not a
// full path), same granularity get_all_process_identities() already gives.
const std::unordered_set<std::string>& protected_process_names() {
    static const std::unordered_set<std::string> kNames = {
        "system",       "system idle process", "registry", "smss.exe",  "csrss.exe",
        "wininit.exe",  "winlogon.exe",        "services.exe", "lsass.exe", "svchost.exe",
        "explorer.exe", "sysintel.exe",
    };
    return kNames;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

ActionBroker::ActionBroker(SqliteStore& store) : store_(store) {}

ActionOutcome ActionBroker::execute(const ActionRequest& request, bool approved) {
    // Permission check: an explicit allowlist. Anything not named here is
    // refused outright, before any state is even read.
    if (request.action_type == "change_power_mode") {
        return handle_change_power_mode(request, approved);
    }
    if (request.action_type == "suspend_process") {
        return handle_suspend_process(request, approved);
    }

    ActionOutcome outcome;
    outcome.known_action = false;
    outcome.approved = approved;
    outcome.message = "unknown action_type: '" + request.action_type + "'";
    return outcome;
}

ActionOutcome ActionBroker::handle_change_power_mode(const ActionRequest& request, bool approved) {
    ActionOutcome outcome;
    outcome.known_action = true;
    outcome.approved = approved;

    auto level_it = request.params.find("level");
    if (level_it == request.params.end()) {
        outcome.message =
            "missing required param 'level' (best_power_efficiency|best_performance)";
        return outcome;
    }

    auto level = power_mode_from_name(level_it->second);
    if (!level.has_value()) {
        outcome.message = "unrecognized level '" + level_it->second + "'";
        return outcome;
    }

    std::string previous = get_dc_power_mode_raw_guid();
    std::string target = guid_to_string(power_mode_guid(*level));
    outcome.previous_state = previous;
    outcome.new_state = target;

    if (previous == target) {
        outcome.success = true;
        outcome.executed = false;
        outcome.message =
            "already at '" + std::string(power_mode_name(*level)) + "' -- no change needed";
        return outcome;
    }

    if (!approved) {
        outcome.message = "dry run: would change DC power mode to '" +
                           std::string(power_mode_name(*level)) + "'. Pass approval to apply.";
        return outcome;
    }

    // Set both AC and DC together: Windows tracks them independently, but a
    // user asking to "switch power mode" means the slider they can see, and
    // that's the AC one whenever the machine happens to be plugged in --
    // DC-only here previously meant the action silently did nothing visible
    // for anyone on AC power at the time.
    bool dc_ok = set_dc_power_mode(*level);
    bool ac_ok = set_ac_power_mode(*level);
    bool ok = dc_ok && ac_ok;
    outcome.executed = true;
    outcome.success = ok;
    if (ok) {
        outcome.message = "changed and verified";
    } else if (dc_ok) {
        outcome.message = "changed on battery, but PowerSetUserConfiguredACPowerMode failed or did not verify";
    } else if (ac_ok) {
        outcome.message = "changed while plugged in, but PowerSetUserConfiguredDCPowerMode failed or did not verify";
    } else {
        outcome.message = "PowerSetUserConfigured{AC,DC}PowerMode both failed or did not verify";
    }

    ActionRecord record;
    record.action_type = request.action_type;
    record.reason = request.reason;
    record.previous_state = previous;
    record.new_state = target;
    record.success = ok;
    // Even a failed attempt might have partially applied -- only promise a
    // rollback path when we know exactly what to restore and that the
    // change is real.
    record.rollback_available = ok;
    outcome.action_id = store_.record_action(record);

    return outcome;
}

ActionOutcome ActionBroker::handle_suspend_process(const ActionRequest& request, bool approved) {
    ActionOutcome outcome;
    outcome.known_action = true;
    outcome.approved = approved;

    auto pid_it = request.params.find("pid");
    if (pid_it == request.params.end()) {
        outcome.message = "missing required param 'pid'";
        return outcome;
    }

    uint32_t pid = 0;
    try {
        pid = static_cast<uint32_t>(std::stoul(pid_it->second));
    } catch (const std::exception&) {
        outcome.message = "invalid pid '" + pid_it->second + "' (must be numeric)";
        return outcome;
    }

    if (pid == GetCurrentProcessId()) {
        outcome.message = "refusing to suspend this agent's own process";
        return outcome;
    }
    if (pid == 0 || pid == 4) {
        outcome.message = "refusing to suspend a reserved system pid (" + std::to_string(pid) + ")";
        return outcome;
    }

    std::string name;
    for (const auto& proc : get_all_process_identities()) {
        if (proc.pid == pid) {
            name = proc.name;
            break;
        }
    }
    if (name.empty()) {
        outcome.message = "no running process with pid " + std::to_string(pid);
        return outcome;
    }
    if (protected_process_names().count(to_lower(name)) > 0) {
        outcome.message = "refusing to suspend '" + name + "' -- a protected system process";
        return outcome;
    }

    // The pid, not a word like "running", is what rollback actually needs
    // to restore -- the same role a power-mode GUID plays in previous_state
    // above: "the exact thing to hand back to whatever undoes this."
    outcome.previous_state = pid_it->second;
    outcome.new_state = "suspended";

    if (!approved) {
        outcome.message = "dry run: would suspend '" + name + "' (pid " + std::to_string(pid) +
                           "). Pass approval to apply.";
        return outcome;
    }

    bool ok = suspend_process(pid);
    outcome.executed = true;
    outcome.success = ok;
    outcome.message = ok ? ("suspended '" + name + "' (pid " + std::to_string(pid) + ")")
                          : "OpenProcess/NtSuspendProcess failed";

    ActionRecord record;
    record.action_type = request.action_type;
    record.reason = request.reason;
    record.previous_state = pid_it->second;
    record.new_state = "suspended";
    record.success = ok;
    record.rollback_available = ok;
    outcome.action_id = store_.record_action(record);

    return outcome;
}

ActionOutcome ActionBroker::rollback(const std::string& action_id, bool approved) {
    ActionOutcome outcome;
    outcome.approved = approved;

    auto record = store_.get_action(action_id);
    if (!record.has_value()) {
        outcome.message = "no such action id: " + action_id;
        return outcome;
    }
    outcome.known_action = true;

    if (!record->rollback_available) {
        outcome.message = "this action has no rollback available";
        return outcome;
    }
    if (record->rolled_back) {
        outcome.message = "this action was already rolled back";
        return outcome;
    }

    // Dispatch on what's actually being undone, the same allowlist pattern
    // execute() uses -- rollback isn't one mechanism, it's one per action
    // type, because "restore the previous state" means something different
    // for a power-mode GUID than for a suspended pid.
    if (record->action_type == "change_power_mode") {
        return rollback_change_power_mode(action_id, *record, approved);
    }
    if (record->action_type == "suspend_process") {
        return rollback_suspend_process(action_id, *record, approved);
    }

    outcome.message = "no rollback handler for action_type '" + record->action_type + "'";
    return outcome;
}

ActionOutcome ActionBroker::rollback_change_power_mode(const std::string& action_id,
                                                        const ActionRecord& record, bool approved) {
    ActionOutcome outcome;
    outcome.known_action = true;
    outcome.approved = approved;

    std::string current = get_dc_power_mode_raw_guid();
    outcome.previous_state = current;
    outcome.new_state = record.previous_state;

    if (!approved) {
        outcome.message = "dry run: would restore power mode (AC and DC) to its state before " +
                           action_id + ". Pass approval to apply.";
        return outcome;
    }

    // record.previous_state is a DC GUID, but AC and DC share the same pair
    // of overlay GUIDs (best_power_efficiency/best_performance), so it's
    // equally valid to restore both to it -- matches how the forward action
    // above sets both to the same target.
    bool dc_ok = set_dc_power_mode_raw_guid(record.previous_state);
    bool ac_ok = set_ac_power_mode_raw_guid(record.previous_state);
    bool ok = dc_ok && ac_ok;
    outcome.executed = true;
    outcome.success = ok;
    outcome.message = ok ? "rolled back and verified" : "rollback failed or did not verify";

    if (ok) {
        store_.mark_action_rolled_back(action_id);
        record_rollback_audit(action_id, "change_power_mode", current, record.previous_state);
    }

    return outcome;
}

ActionOutcome ActionBroker::rollback_suspend_process(const std::string& action_id,
                                                      const ActionRecord& record, bool approved) {
    ActionOutcome outcome;
    outcome.known_action = true;
    outcome.approved = approved;
    outcome.previous_state = "suspended";
    outcome.new_state = "running";

    uint32_t pid = 0;
    try {
        pid = static_cast<uint32_t>(std::stoul(record.previous_state));
    } catch (const std::exception&) {
        outcome.message = "corrupt action record: previous_state is not a numeric pid";
        return outcome;
    }

    if (!approved) {
        outcome.message =
            "dry run: would resume pid " + std::to_string(pid) + ". Pass approval to apply.";
        return outcome;
    }

    bool ok = resume_process(pid);
    outcome.executed = true;
    outcome.success = ok;
    outcome.message = ok ? ("resumed pid " + std::to_string(pid))
                          : "OpenProcess/NtResumeProcess failed -- the process may have exited";

    if (ok) {
        store_.mark_action_rolled_back(action_id);
        record_rollback_audit(action_id, "suspend_process", "suspended", "running");
    }

    return outcome;
}

void ActionBroker::record_rollback_audit(const std::string& action_id,
                                          const std::string& original_type,
                                          const std::string& previous_state,
                                          const std::string& new_state) {
    // Logged as its own audit entry so the trail stays complete -- but it
    // doesn't get a rollback of its own; that's not a concept this project
    // needs.
    ActionRecord rollback_record;
    rollback_record.action_type = "rollback";
    rollback_record.reason = "rollback of " + action_id + " (" + original_type + ")";
    rollback_record.previous_state = previous_state;
    rollback_record.new_state = new_state;
    rollback_record.success = true;
    rollback_record.rollback_available = false;
    store_.record_action(rollback_record);
}

}  // namespace sysintel
