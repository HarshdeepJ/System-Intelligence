#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "availability.hpp"

namespace sysintel {

// Things that don't change often (contrast with SystemState, which does).
// Worth re-collecting on startup and every few hours, not every tick.

struct CpuInventory {
    Reading<std::string> model;
    Reading<int> physical_cores;
    Reading<int> logical_processors;
};

struct GpuInventory {
    std::string vendor;  // "NVIDIA" | "AMD" | "Intel" | "Unknown" -- inferred from name
    std::string model;
    Reading<uint64_t> vram_bytes;
};

struct OsInventory {
    Reading<std::string> caption;  // e.g. "Microsoft Windows 11 Pro"
    Reading<std::string> version;
    Reading<std::string> build_number;
};

struct MemoryInventory {
    Reading<int> dimm_count;
    Reading<uint64_t> total_capacity_bytes;
};

struct DiskInventory {
    std::string model;
    Reading<uint64_t> size_bytes;
};

struct BatteryInventory {
    Reading<std::string> chemistry;
    Reading<int> design_capacity_mwh;
    Reading<int> full_charge_capacity_mwh;
};

struct SystemInventory {
    Reading<std::string> manufacturer;
    Reading<std::string> model;
    CpuInventory cpu;
    std::vector<GpuInventory> gpus;
    OsInventory os;
    MemoryInventory memory;
    std::vector<DiskInventory> disks;
    BatteryInventory battery;
};

SystemInventory collect_system_inventory();

}  // namespace sysintel
