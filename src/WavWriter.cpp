#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mdsh {
namespace {

void write_u32le(std::ofstream& f, uint32_t v) {
    char bytes[4] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                      static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff)};
    f.write(bytes, 4);
}

void write_u16le(std::ofstream& f, uint16_t v) {
    char bytes[2] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
    f.write(bytes, 2);
}

}  // namespace

bool WavWriter::open(const std::string& path, uint32_t sample_rate, std::string& error) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "could not create " + path;
        return false;
    }
    sample_rate_ = sample_rate;
    sample_count_ = 0;

    // Placeholder header -- sizes get patched in on close().
    file_.write("RIFF", 4);
    write_u32le(file_, 0);
    file_.write("WAVE", 4);
    file_.write("fmt ", 4);
    write_u32le(file_, 16);
    write_u16le(file_, 1);               // PCM
    write_u16le(file_, 1);               // mono
    write_u32le(file_, sample_rate_);
    write_u32le(file_, sample_rate_ * 2);  // byte rate (16-bit mono)
    write_u16le(file_, 2);               // block align
    write_u16le(file_, 16);              // bits per sample
    file_.write("data", 4);
    write_u32le(file_, 0);
    return true;
}

void WavWriter::write(const float* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        const int16_t pcm = static_cast<int16_t>(std::lround(clamped * 32767.0f));
        write_u16le(file_, static_cast<uint16_t>(pcm));
    }
    sample_count_ += count;
}

void WavWriter::set_comment(const std::string& text) {
    comment_ = text;
}

bool WavWriter::close(std::string& error) {
    if (!file_) {
        error = "file was not open";
        return false;
    }
    const uint32_t data_bytes = static_cast<uint32_t>(sample_count_ * 2);
    uint32_t riff_size = 36 + data_bytes;

    // Optional LIST/INFO/ICMT comment chunk, appended after "data" -- any
    // reader that skips unknown chunks (virtually all of them) still
    // plays the file; one that reads ICMT gets this render's exact
    // parameters/trig-pattern/neighbour-WAVs back, so the file alone
    // answers "what produced this?" without a separate sidecar.
    if (!comment_.empty()) {
        std::string icmt = comment_;
        if (icmt.size() % 2 != 0) icmt.push_back('\0');  // RIFF chunks are word-aligned
        const uint32_t icmt_len = static_cast<uint32_t>(comment_.size());
        const uint32_t list_payload = 4 /*"INFO"*/ + 8 /*ICMT header*/ + static_cast<uint32_t>(icmt.size());

        file_.seekp(0, std::ios::end);
        file_.write("LIST", 4);
        write_u32le(file_, list_payload);
        file_.write("INFO", 4);
        file_.write("ICMT", 4);
        write_u32le(file_, icmt_len);
        file_.write(icmt.data(), static_cast<std::streamsize>(icmt.size()));

        riff_size += 8 + list_payload;  // LIST chunk header (8) + its payload
    }

    file_.seekp(4);
    write_u32le(file_, riff_size);
    file_.seekp(40);
    write_u32le(file_, data_bytes);
    file_.close();
    return true;
}

}  // namespace mdsh
