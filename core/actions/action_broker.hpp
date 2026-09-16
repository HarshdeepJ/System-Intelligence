#pragma once
#include <string>
#include <unordered_map>

#include "../storage/sqlite_store.hpp"

namespace sysintel {

struct ActionRequest {
    std::string action_type;  // "change_power_mode" or "suspend_process"
    std::string reason;
    std::unordered_map<std::string, std::string> params;
};

struct ActionOutcome {
    bool known_action = false;  // permission check: is this a recognized action_type at all?
    bool approved = false;
    bool executed = false;
    bool success = false;
    std::string previous_state;
    std::string new_state;
    std::string message;
    std::string action_id;  // set once an execution is recorded (never for a dry run)
};

// Every mutation this project can make to the machine flows through here:
// permission check -> (only if approved) execute -> verify -> audit record.
// There is no other path from a diagnosis/recommendation to an actual
// change -- not from the Python agent, not from any CLI command. This is
// the PRD's Action Broker.
class ActionBroker {
public:
    explicit ActionBroker(SqliteStore& store);

    // approved=false returns a dry-run preview: previous/new state are
    // still computed so the caller can see exactly what would happen, but
    // nothing is changed and nothing is logged. approved=true executes and
    // always records an audit entry, whether it succeeds or fails.
    ActionOutcome execute(const ActionRequest& request, bool approved);

    // Restores the exact previous_state recorded for a prior action. Same
    // approved=false/true dry-run/execute split as execute().
    ActionOutcome rollback(const std::string& action_id, bool approved);

private:
    SqliteStore& store_;

    ActionOutcome handle_change_power_mode(const ActionRequest& request, bool approved);
    ActionOutcome handle_suspend_process(const ActionRequest& request, bool approved);

    ActionOutcome rollback_change_power_mode(const std::string& action_id,
                                              const ActionRecord& record, bool approved);
    ActionOutcome rollback_suspend_process(const std::string& action_id, const ActionRecord& record,
                                            bool approved);
    void record_rollback_audit(const std::string& action_id, const std::string& original_type,
                                const std::string& previous_state, const std::string& new_state);
};

}  // namespace sysintel
