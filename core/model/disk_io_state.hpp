#pragma once
#include <string>
#include <vector>

#include "availability.hpp"

namespace sysintel {

struct DiskIoState {
    std::string instance;  // PDH instance name, e.g. "0 C: D:"
    Reading<double> read_bytes_per_sec;
    Reading<double> write_bytes_per_sec;
    Reading<double> reads_per_sec;
    Reading<double> writes_per_sec;
    Reading<double> queue_length;
};

// Live disk activity -- contrast with core/inventory's static disk model/
// size. Takes ~1 second: PDH's rate counters need two samples spaced apart,
// same trick the CPU collector uses.
std::vector<DiskIoState> get_disk_io_state();

}  // namespace sysintel
