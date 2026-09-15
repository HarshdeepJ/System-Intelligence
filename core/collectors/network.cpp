#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // otherwise windows.h's min/max macros break std::max below

// netioapi.h (pulled in by iphlpapi.h) documents that it must be included
// after ws2def.h/ws2ipdef.h. winsock2.h alone doesn't pull those in early
// enough to satisfy it -- confirmed empirically after MIB_IF_TABLE2 and
// friends silently failed to declare with just winsock2.h; explicitly
// including ws2def.h/ws2ipdef.h first fixed it. These must all come before
// windows.h (whichever header pulls it in first honors NOMINMAX/LEAN_AND_MEAN,
// and later #includes of an already-guarded header are no-ops).
#include <winsock2.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <windows.h>

#include <ipifcons.h>
#include <iphlpapi.h>
#include <wlanapi.h>

#include "../model/network_state.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wlanapi.lib")

namespace sysintel {

namespace {

std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');  // len includes the null terminator
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

struct OctetSample {
    uint64_t in_octets = 0;
    uint64_t out_octets = 0;
};

// Interface *type* alone (Ethernet/802.11) isn't enough to identify real
// adapters -- Windows creates many internal NDIS filter-driver "shadow"
// rows sharing the same type (WFP filters, QoS packet scheduler bindings,
// Wi-Fi Direct virtual adapters), and they outnumber actual hardware
// several-to-one. Confirmed empirically: an unfiltered pass on this
// machine reported ~25 "adapters" for what is really one Wi-Fi card.
// HardwareInterface is the flag Windows itself exposes to make this
// distinction.
bool is_real_adapter(const MIB_IF_ROW2& row) {
    bool right_type = row.Type == IF_TYPE_ETHERNET_CSMACD || row.Type == IF_TYPE_IEEE80211;
    return right_type && row.InterfaceAndOperStatusFlags.HardwareInterface &&
           !row.InterfaceAndOperStatusFlags.FilterInterface;
}

std::unordered_map<uint64_t, OctetSample> snapshot_octets(const MIB_IF_TABLE2* table) {
    std::unordered_map<uint64_t, OctetSample> result;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& row = table->Table[i];
        if (!is_real_adapter(row)) {
            continue;
        }
        result[row.InterfaceLuid.Value] = {row.InOctets, row.OutOctets};
    }
    return result;
}

// Best-effort: the WLAN API reports on the currently *connected* Wi-Fi
// interface(s), not queryable per-adapter by LUID the way wired throughput
// is -- so this returns the signal quality of whichever Wi-Fi interface is
// connected, if any, rather than being matched precisely to one adapter row.
// Returns -1 if there's no connected Wi-Fi interface or the WLAN service
// itself is unreachable.
int connected_wifi_signal_percent() {
    HANDLE handle = nullptr;
    DWORD negotiated_version = 0;
    if (WlanOpenHandle(2, nullptr, &negotiated_version, &handle) != ERROR_SUCCESS) {
        return -1;
    }

    int result = -1;
    PWLAN_INTERFACE_INFO_LIST if_list = nullptr;
    if (WlanEnumInterfaces(handle, nullptr, &if_list) == ERROR_SUCCESS && if_list) {
        for (DWORD i = 0; i < if_list->dwNumberOfItems; ++i) {
            const auto& info = if_list->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected) {
                continue;
            }

            PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
            DWORD size = 0;
            WLAN_OPCODE_VALUE_TYPE opcode_type;
            if (WlanQueryInterface(handle, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                                   nullptr, &size, reinterpret_cast<PVOID*>(&attrs),
                                   &opcode_type) == ERROR_SUCCESS &&
                attrs) {
                result = static_cast<int>(attrs->wlanAssociationAttributes.wlanSignalQuality);
                WlanFreeMemory(attrs);
                break;
            }
        }
        WlanFreeMemory(if_list);
    }

    WlanCloseHandle(handle, nullptr);
    return result;
}

}  // namespace

std::vector<NetworkAdapterState> get_network_state() {
    std::vector<NetworkAdapterState> result;

    MIB_IF_TABLE2* table_before = nullptr;
    if (GetIfTable2(&table_before) != NO_ERROR || !table_before) {
        return result;
    }
    auto before = snapshot_octets(table_before);
    FreeMibTable(table_before);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    MIB_IF_TABLE2* table_after = nullptr;
    if (GetIfTable2(&table_after) != NO_ERROR || !table_after) {
        return result;
    }

    int wifi_signal = connected_wifi_signal_percent();

    for (ULONG i = 0; i < table_after->NumEntries; ++i) {
        const auto& row = table_after->Table[i];
        if (!is_real_adapter(row)) {
            continue;
        }

        NetworkAdapterState state;
        state.name = wide_to_utf8(row.Description);
        state.type = (row.Type == IF_TYPE_IEEE80211) ? "wifi" : "ethernet";
        state.operational = (row.OperStatus == IfOperStatusUp);

        uint64_t link_speed = std::max(row.ReceiveLinkSpeed, row.TransmitLinkSpeed);
        state.link_speed_bps =
            link_speed > 0 ? Reading<uint64_t>::ok(link_speed) : Reading<uint64_t>::unavailable();

        auto it = before.find(row.InterfaceLuid.Value);
        if (it != before.end()) {
            double sent_delta =
                static_cast<double>(row.OutOctets) - static_cast<double>(it->second.out_octets);
            double recv_delta =
                static_cast<double>(row.InOctets) - static_cast<double>(it->second.in_octets);
            // ~1 second elapsed (the sleep above) -- same "trust the fixed
            // interval, don't re-measure wall clock" approach cpu.cpp uses.
            state.bytes_sent_per_sec = Reading<double>::ok(std::max(0.0, sent_delta));
            state.bytes_received_per_sec = Reading<double>::ok(std::max(0.0, recv_delta));
        } else {
            state.bytes_sent_per_sec = Reading<double>::unavailable();
            state.bytes_received_per_sec = Reading<double>::unavailable();
        }

        if (state.type == "wifi") {
            state.wifi_signal_percent =
                wifi_signal >= 0 ? Reading<int>::ok(wifi_signal) : Reading<int>::unavailable();
        } else {
            state.wifi_signal_percent = Reading<int>::unsupported();
        }

        result.push_back(std::move(state));
    }

    FreeMibTable(table_after);
    return result;
}

}  // namespace sysintel
