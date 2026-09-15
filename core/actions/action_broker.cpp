#include "action_broker.hpp"

#include "power_scheme.hpp"

namespace sysintel {

ActionBroker::ActionBroker(SqliteStore& store) : store_(store) {}

ActionOutcome ActionBroker::execute(const ActionRequest& request, bool approved) {
    // Permission check: an explicit allowlist. Anything not named here is
    // refused outright, before any state is even read.
    if (request.action_type == "change_power_mode") {
        return handle_change_power_mode(request, approved);
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

    bool ok = set_dc_power_mode(*level);
    outcome.executed = true;
    outcome.success = ok;
    outcome.message =
        ok ? "changed and verified" : "PowerSetUserConfiguredDCPowerMode failed or did not verify";

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

    std::string current = get_dc_power_mode_raw_guid();
    outcome.previous_state = current;
    outcome.new_state = record->previous_state;

    if (!approved) {
        outcome.message = "dry run: would restore DC power mode to its state before " +
                           action_id + ". Pass approval to apply.";
        return outcome;
    }

    bool ok = set_dc_power_mode_raw_guid(record->previous_state);
    outcome.executed = true;
    outcome.success = ok;
    outcome.message = ok ? "rolled back and verified" : "rollback failed or did not verify";

    if (ok) {
        store_.mark_action_rolled_back(action_id);

        // Log the rollback itself as its own audit entry too, so the trail
        // stays complete -- but it doesn't get a rollback of its own; that's
        // not a concept this project needs.
        ActionRecord rollback_record;
        rollback_record.action_type = "rollback";
        rollback_record.reason = "rollback of " + action_id;
        rollback_record.previous_state = current;
        rollback_record.new_state = record->previous_state;
        rollback_record.success = true;
        rollback_record.rollback_available = false;
        store_.record_action(rollback_record);
    }

    return outcome;
}

}  // namespace sysintel
