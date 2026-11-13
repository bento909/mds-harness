#include "MdsTransform.h"

#include <cmath>

namespace mdsh {

uint32_t convert_param(uint8_t conversion, uint8_t raw, uint8_t min_value, double bpm) {
    constexpr uint32_t kMask24 = 0x00ffffffu;
    switch (conversion) {
        case 0:  // Raw 14-bit: setting in units of 1/128; 64 becomes 8192
            return static_cast<uint32_t>(raw) * 128u;
        case 1:  // Unipolar Q23: positive level; 64 becomes 0.5
            return raw >= 127 ? 0x7fffffu : static_cast<uint32_t>(raw) << 16;
        case 2: {  // Squared Q23: squared level; 64 becomes 0.25
            const uint32_t v = raw;
            return (v * v) << 9;
        }
        case 3:  // Bipolar Q23: signed level; 0 becomes -1, 64 becomes zero
            return (((static_cast<uint32_t>(raw) << 17)) - 0x800000u) & kMask24;
        case 4: {  // Pitch: oscillator increment using active tuning
            const double ratio = std::log2(538240.0 / 1625.0) * static_cast<double>(raw) / 127.0;
            return static_cast<uint32_t>(std::lround(1625.0 * std::exp2(ratio))) & kMask24;
        }
        case 5:  // Boolean: 0 or 1
            return raw != 0 ? 1u : 0u;
        case 6:  // Index: setting minus the declared minimum
            return static_cast<uint32_t>(raw) - static_cast<uint32_t>(min_value);
        case 7: {  // Decay: per-sample envelope multiplier, 0 shortest, 127 longest
            const double coefficient = 1.0 - std::pow(2.0, -(1.0 + 21.0 * static_cast<double>(raw) / 127.0));
            const auto value = static_cast<uint32_t>(std::lround(coefficient * 8388608.0));
            return value > 0x7fffffu ? 0x7fffffu : value;
        }
        case 8: {  // Tempo: length in samples at the current tempo
            const auto bpm_u = static_cast<uint32_t>(bpm);
            if (bpm_u == 0) return 0;
            return ((270950400u / (24u * bpm_u)) * 60u / 4096u) * static_cast<uint32_t>(raw);
        }
        default:
            return 0;
    }
}

}  // namespace mdsh
