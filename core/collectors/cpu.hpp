#pragma once

namespace sysintel {

struct CpuSnapshot {
    double total_utilization_percent = 0.0;
};

// Takes ~1 second to return: the underlying performance counter needs two
// samples spaced apart to compute a rate.
CpuSnapshot get_cpu_snapshot();

}  // namespace sysintel
