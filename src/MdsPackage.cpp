#include "MdsPackage.h"

#include <fstream>
#include <vector>

namespace mdsh {
namespace {

uint16_t read_u16(const std::vector<uint8_t>& d, size_t at) {
    return static_cast<uint16_t>((d[at] << 8) | d[at + 1]);
}

uint32_t read_u32(const std::vector<uint8_t>& d, size_t at) {
    return (static_cast<uint32_t>(d[at]) << 24) | (static_cast<uint32_t>(d[at + 1]) << 16) |
           (static_cast<uint32_t>(d[at + 2]) << 8) | static_cast<uint32_t>(d[at + 3]);
}

std::string read_name(const std::vector<uint8_t>& d, size_t at, size_t max_len) {
    std::string s;
    for (size_t i = 0; i < max_len; ++i) {
        const uint8_t c = d[at + i];
        if (c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

}  // namespace

bool MdsPackage::load(const std::string& path, MdsPackage& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open " + path;
        return false;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (d.size() < 128 || d[0] != 'M' || d[1] != 'D' || d[2] != 'S' || d[3] != '1') {
        error = path + " is not an MDS1 package";
        return false;
    }

    const uint16_t program_words = read_u16(d, 22);
    const uint16_t import_count = read_u16(d, 24);
    out.x_words = read_u16(d, 26);
    out.y_words = read_u16(d, 28);
    const uint32_t program_offset = read_u32(d, 32);
    const uint32_t program_length = read_u32(d, 36);
    const uint32_t relocation_offset = read_u32(d, 44);
    const uint32_t import_offset = read_u32(d, 48);
    const uint32_t description_offset = read_u32(d, 56);
    out.init_offset = read_u16(d, 64);
    out.trig_offset = read_u16(d, 66);
    out.render_offset = read_u16(d, 68);
    out.mk2_cycles = read_u16(d, 70);
    out.mk1_cycles = read_u16(d, 72);
    out.mk1_sram_cycles = read_u16(d, 74);

    if (program_offset + program_length > d.size()) {
        error = "program data runs past the end of the file";
        return false;
    }
    if (program_length != static_cast<uint32_t>(program_words) * 3) {
        error = "program word count does not match the stored program length";
        return false;
    }

    out.program.assign(program_words, 0);
    for (uint16_t i = 0; i < program_words; ++i) {
        const size_t at = program_offset + static_cast<size_t>(i) * 3;
        out.program[i] = (static_cast<uint32_t>(d[at]) << 16) | (static_cast<uint32_t>(d[at + 1]) << 8) |
                          static_cast<uint32_t>(d[at + 2]);
    }

    out.relocated.assign(program_words, false);
    const uint16_t relocation_bytes = static_cast<uint16_t>((program_words + 7) / 8);
    if (relocation_offset + relocation_bytes > d.size()) {
        error = "relocation flags run past the end of the file";
        return false;
    }
    for (uint16_t i = 0; i < program_words; ++i) {
        const uint8_t byte = d[relocation_offset + i / 8];
        out.relocated[i] = (byte & (1u << (i % 8))) != 0;
    }

    out.imports.clear();
    for (uint16_t i = 0; i < import_count; ++i) {
        const size_t at = import_offset + static_cast<size_t>(i) * 4;
        if (at + 4 > d.size()) {
            error = "import table runs past the end of the file";
            return false;
        }
        MdsImport item;
        item.word = read_u16(d, at);
        item.kind = d[at + 2];
        item.symbol = d[at + 3];
        out.imports.push_back(item);
    }

    if (description_offset + 16 > d.size()) {
        error = "machine description runs past the end of the file";
        return false;
    }
    if (d[description_offset] != 'M' || d[description_offset + 1] != 'D' || d[description_offset + 2] != 'M' ||
        d[description_offset + 3] != '1') {
        error = "machine description is not an MDM1 block";
        return false;
    }
    const uint8_t flags = d[description_offset + 6];
    out.tonal = (flags & 0x01) != 0;
    out.continuous_retrigger = (flags & 0x04) != 0;
    out.neighbour = (flags & 0x08) != 0;
    out.stereo_pair = (flags & 0x10) != 0;

    out.params.assign(8, MdsParam{});
    const size_t params_at = description_offset + 64;
    if (params_at + 8 * 8 > d.size()) {
        error = "parameter descriptions run past the end of the file";
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        const size_t at = params_at + static_cast<size_t>(i) * 8;
        MdsParam& p = out.params[i];
        p.name = read_name(d, at, 4);
        p.default_value = d[at + 4];
        p.min_value = d[at + 5];
        p.max_value = d[at + 6];
        const uint8_t conv_style = d[at + 7];
        p.style = static_cast<uint8_t>((conv_style >> 4) & 0x0f);
        p.conversion = static_cast<uint8_t>(conv_style & 0x0f);
    }

    return true;
}

}  // namespace mdsh
