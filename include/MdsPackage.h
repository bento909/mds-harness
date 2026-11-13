#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Parses the .mds package format (MDS 1.1), per the public spec:
// https://github.com/jmamma/MDS, docs/MDX_MachineDumpStandard.txt
// Written fresh from that spec, not copied from mds-tools (see README.md
// "License / dependencies" for why).
namespace mdsh {

struct MdsImport {
    uint16_t word = 0;   // program word index this import patches
    uint8_t kind = 0;    // 1 = memory resource, 2..5 = callable service
    uint8_t symbol = 0;  // which resource/service (see Symbol enum below)
};

enum Symbol : uint8_t {
    kSymSine = 1,
    kSymInputRing = 2,
    kSymInputCursor = 3,
    kSymMainRing = 4,
    kSymMainCursor = 5,
    kSymTrackMemory = 6,
    kSymTempX = 7,
    kSymTempY = 8,
    kSymSampleInfo = 9,
    kSymSampleRead = 10,
    kSymRamWorkspace = 11,
    kSymRamRecord = 12,
    kSymTriggerInfo = 13,
};

struct MdsParam {
    std::string name;          // up to 4 characters
    uint8_t default_value = 0; // 0..127
    uint8_t min_value = 0;
    uint8_t max_value = 127;
    uint8_t style = 0;         // upper 4 bits of the conversion byte
    uint8_t conversion = 0;    // lower 4 bits of the conversion byte

    bool used() const { return !name.empty(); }
};

struct MdsPackage {
    std::vector<uint32_t> program;  // 24-bit words, MSB-first in the file
    std::vector<bool> relocated;    // same length as program

    std::vector<MdsImport> imports; // ascending by word

    uint16_t init_offset = 0;
    uint16_t trig_offset = 0;
    uint16_t render_offset = 0;

    uint16_t x_words = 0;  // per-track X data words the machine uses
    uint16_t y_words = 0;  // per-track Y data words the machine uses

    uint16_t mk2_cycles = 0;
    uint16_t mk1_cycles = 0;
    uint16_t mk1_sram_cycles = 0;

    bool tonal = false;
    bool neighbour = false;
    bool stereo_pair = false;
    bool continuous_retrigger = false;

    std::vector<MdsParam> params;  // always 8 entries; unused ones have an empty name

    static bool load(const std::string& path, MdsPackage& out, std::string& error);
};

}
