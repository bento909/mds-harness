#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mdsh {

// Minimal reader for the same 16-bit PCM mono format WavWriter produces.
// Loaded samples are returned already scaled to Q23 (the DSP's own
// fixed-point format), so callers can feed them straight into Y-memory.
class WavReader {
public:
    static bool load(const std::string& path, std::vector<int32_t>& out_q23_samples, std::string& error);
};

}  // namespace mdsh
