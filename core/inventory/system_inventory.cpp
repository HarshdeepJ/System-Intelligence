#include "../model/system_inventory.hpp"

#include <algorithm>
#include <cctype>

#include "../providers/windows/wmi_client.hpp"

namespace sysintel {

namespace {

Reading<std::string> field_str(const WmiRow& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end() || it->second.empty()) {
        return Reading<std::string>::unavailable();
    }
    return Reading<std::string>::ok(it->second);
}

Reading<int> field_int(const WmiRow& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end() || it->second.empty()) {
        return Reading<int>::unavailable();
    }
    try {
        return Reading<int>::ok(std::stoi(it->second));
    } catch (...) {
        return Reading<int>::unavailable();
    }
}

Reading<uint64_t> field_u64(const WmiRow& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end() || it->second.empty()) {
        return Reading<uint64_t>::unavailable();
    }
    try {
        return Reading<uint64_t>::ok(static_cast<uint64_t>(std::stoull(it->second)));
    } catch (...) {
        return Reading<uint64_t>::unavailable();
    }
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string infer_gpu_vendor(const std::string& name) {
    std::string lower = to_lower(name);
    if (lower.find("nvidia") != std::string::npos) return "NVIDIA";
    if (lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos)
        return "AMD";
    if (lower.find("intel") != std::string::npos) return "Intel";
    return "Unknown";
}

}  // namespace

SystemInventory collect_system_inventory() {
    SystemInventory inv;

    WmiClient wmi;
    if (!wmi.ok()) {
        // WMI itself is unavailable -- every field is explicitly marked
        // unavailable rather than silently left as a default-constructed
        // (and misleadingly kOk) Reading.
        inv.manufacturer = Reading<std::string>::unavailable();
        inv.model = Reading<std::string>::unavailable();
        inv.cpu.model = Reading<std::string>::unavailable();
        inv.cpu.physical_cores = Reading<int>::unavailable();
        inv.cpu.logical_processors = Reading<int>::unavailable();
        inv.os.caption = Reading<std::string>::unavailable();
        inv.os.version = Reading<std::string>::unavailable();
        inv.os.build_number = Reading<std::string>::unavailable();
        inv.memory.dimm_count = Reading<int>::unavailable();
        inv.memory.total_capacity_bytes = Reading<uint64_t>::unavailable();
        inv.battery.chemistry = Reading<std::string>::unavailable();
        inv.battery.design_capacity_mwh = Reading<int>::unavailable();
        inv.battery.full_charge_capacity_mwh = Reading<int>::unavailable();
        return inv;
    }

    auto system_rows = wmi.query("SELECT Manufacturer, Model FROM Win32_ComputerSystem");
    if (!system_rows.empty()) {
        inv.manufacturer = field_str(system_rows[0], "Manufacturer");
        inv.model = field_str(system_rows[0], "Model");
    } else {
        inv.manufacturer = Reading<std::string>::unavailable();
        inv.model = Reading<std::string>::unavailable();
    }

    auto cpu_rows =
        wmi.query("SELECT Name, NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor");
    if (!cpu_rows.empty()) {
        inv.cpu.model = field_str(cpu_rows[0], "Name");
        inv.cpu.physical_cores = field_int(cpu_rows[0], "NumberOfCores");
        inv.cpu.logical_processors = field_int(cpu_rows[0], "NumberOfLogicalProcessors");
    } else {
        inv.cpu.model = Reading<std::string>::unavailable();
        inv.cpu.physical_cores = Reading<int>::unavailable();
        inv.cpu.logical_processors = Reading<int>::unavailable();
    }

    auto gpu_rows = wmi.query("SELECT Name, AdapterRAM FROM Win32_VideoController");
    for (const auto& row : gpu_rows) {
        GpuInventory gpu;
        auto name = field_str(row, "Name");
        gpu.model = name.value.value_or("Unknown GPU");
        gpu.vendor = infer_gpu_vendor(gpu.model);
        gpu.vram_bytes = field_u64(row, "AdapterRAM");
        inv.gpus.push_back(std::move(gpu));
    }

    auto os_rows = wmi.query("SELECT Caption, Version, BuildNumber FROM Win32_OperatingSystem");
    if (!os_rows.empty()) {
        inv.os.caption = field_str(os_rows[0], "Caption");
        inv.os.version = field_str(os_rows[0], "Version");
        inv.os.build_number = field_str(os_rows[0], "BuildNumber");
    } else {
        inv.os.caption = Reading<std::string>::unavailable();
        inv.os.version = Reading<std::string>::unavailable();
        inv.os.build_number = Reading<std::string>::unavailable();
    }

    auto mem_rows = wmi.query("SELECT Capacity FROM Win32_PhysicalMemory");
    if (!mem_rows.empty()) {
        uint64_t total = 0;
        for (const auto& row : mem_rows) {
            auto cap = field_u64(row, "Capacity");
            if (cap.value.has_value()) {
                total += *cap.value;
            }
        }
        inv.memory.dimm_count = Reading<int>::ok(static_cast<int>(mem_rows.size()));
        inv.memory.total_capacity_bytes = Reading<uint64_t>::ok(total);
    } else {
        inv.memory.dimm_count = Reading<int>::unavailable();
        inv.memory.total_capacity_bytes = Reading<uint64_t>::unavailable();
    }

    auto disk_rows = wmi.query("SELECT Model, Size FROM Win32_DiskDrive");
    for (const auto& row : disk_rows) {
        DiskInventory disk;
        auto model = field_str(row, "Model");
        disk.model = model.value.value_or("Unknown disk");
        disk.size_bytes = field_u64(row, "Size");
        inv.disks.push_back(std::move(disk));
    }

    // Win32_Battery's Chemistry/DesignCapacity/FullChargeCapacity are
    // frequently unpopulated depending on the embedded controller/OEM --
    // exactly the "unsupported vs unavailable" distinction this model
    // exists for: the class instance exists (there IS a battery), but these
    // specific fields may still come back empty.
    auto battery_rows =
        wmi.query("SELECT Chemistry, DesignCapacity, FullChargeCapacity FROM Win32_Battery");
    if (!battery_rows.empty()) {
        auto chemistry_code = field_int(battery_rows[0], "Chemistry");
        if (chemistry_code.value.has_value()) {
            // Win32_Battery.Chemistry is a coded value: 1=Other, 2=Unknown,
            // 3=Lead Acid, 4=Nickel Cadmium, 5=Nickel Metal Hydride,
            // 6=Lithium-ion, 7=Zinc air, 8=Lithium Polymer.
            static const char* kChemistryNames[] = {
                "Other",  "Unknown",  "Lead Acid",           "Nickel Cadmium",
                "Nickel Metal Hydride", "Lithium-ion", "Zinc air", "Lithium Polymer",
            };
            int code = *chemistry_code.value;
            if (code >= 1 && code <= 8) {
                inv.battery.chemistry = Reading<std::string>::ok(kChemistryNames[code - 1]);
            } else {
                inv.battery.chemistry = Reading<std::string>::unavailable();
            }
        } else {
            inv.battery.chemistry = Reading<std::string>::unavailable();
        }
        inv.battery.design_capacity_mwh = field_int(battery_rows[0], "DesignCapacity");
        inv.battery.full_charge_capacity_mwh = field_int(battery_rows[0], "FullChargeCapacity");
    } else {
        // No Win32_Battery instance at all -- genuinely no battery, not
        // merely a missing field.
        inv.battery.chemistry = Reading<std::string>::unsupported();
        inv.battery.design_capacity_mwh = Reading<int>::unsupported();
        inv.battery.full_charge_capacity_mwh = Reading<int>::unsupported();
    }

    return inv;
}

}  // namespace sysintel
