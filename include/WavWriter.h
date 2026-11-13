#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mdsh {

// Minimal 16-bit PCM mono WAV writer. Buffers samples in memory and patches
// the header's size fields on close -- fine for the short render lengths
// this tool deals with.
class WavWriter {
public:
    bool open(const std::string& path, uint32_t sample_rate, std::string& error);
    void write(const float* samples, size_t count);  // expects roughly -1..1

    // Stashes `text` (arbitrary -- this tool always passes a JSON blob of
    // the render's exact parameters/trig pattern/neighbour WAVs, so the
    // file alone answers "what produced this?") into a standard RIFF
    // LIST/INFO/ICMT comment chunk, written on close(). Any WAV reader
    // that ignores unknown chunks (nearly all of them) still plays the
    // file fine; one that reads ICMT sees the comment.
    void set_comment(const std::string& text);

    bool close(std::string& error);

private:
    std::ofstream file_;
    uint32_t sample_rate_ = 44100;
    uint64_t sample_count_ = 0;
    std::string comment_;
};

}  // namespace mdsh
