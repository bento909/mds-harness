#include "WavReader.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace mdsh {
namespace {

uint32_t read_u32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

}  // namespace

bool WavReader::load(const std::string& path, std::vector<int32_t>& out_q23_samples, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = path + " is not a RIFF/WAVE file";
        return false;
    }

    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t* data_ptr = nullptr;
    uint32_t data_bytes = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        char id[5] = {0};
        std::memcpy(id, bytes.data() + pos, 4);
        const uint32_t chunk_size = read_u32le(bytes.data() + pos + 4);
        const size_t chunk_data = pos + 8;
        if (chunk_data + chunk_size > bytes.size()) break;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunk_size >= 16) {
            const uint8_t* f = bytes.data() + chunk_data;
            const uint16_t audio_format = read_u16le(f);
            num_channels = read_u16le(f + 2);
            bits_per_sample = read_u16le(f + 14);
            if (audio_format != 1) {
                error = path + " is not uncompressed PCM";
                return false;
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            data_ptr = bytes.data() + chunk_data;
            data_bytes = chunk_size;
        }

        pos = chunk_data + chunk_size + (chunk_size & 1);  // chunks are word-aligned
    }

    if (!data_ptr || num_channels == 0) {
        error = path + " has no usable fmt/data chunks";
        return false;
    }
    if (bits_per_sample != 16) {
        error = path + " must be 16-bit PCM (got " + std::to_string(bits_per_sample) + "-bit)";
        return false;
    }

    const uint32_t bytes_per_frame = 2u * num_channels;
    const uint32_t frame_count = data_bytes / bytes_per_frame;
    out_q23_samples.clear();
    out_q23_samples.reserve(frame_count);

    for (uint32_t i = 0; i < frame_count; ++i) {
        const uint8_t* frame = data_ptr + static_cast<size_t>(i) * bytes_per_frame;
        int32_t sum = 0;
        for (uint16_t c = 0; c < num_channels; ++c) {
            const int16_t sample16 = static_cast<int16_t>(read_u16le(frame + 2u * c));
            sum += sample16;
        }
        const double mono = (static_cast<double>(sum) / num_channels) / 32767.0;
        out_q23_samples.push_back(static_cast<int32_t>(std::lround(mono * 8388607.0)));
    }
    return true;
}

}  // namespace mdsh
