#pragma once

#include "MdsPackage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dsp56k {
class DSP;
class Memory;
class DefaultMemoryValidator;
class PeripheralsNop;
}  // namespace dsp56k

namespace mdsh {

// Drives one loaded .mds machine through the real dsp56kEmu interpreter:
// install the program, poke parameters, call INIT/TRIG/RENDER, read audio
// back out. No cycle counting, no sandboxing beyond what's needed to run
// correctly -- this is a playback/development tool, not a certifier.
class MachineEngine {
public:
    MachineEngine();
    ~MachineEngine();
    MachineEngine(const MachineEngine&) = delete;
    MachineEngine& operator=(const MachineEngine&) = delete;

    // Loads `package` into P-memory at a fixed internal base, resolves its
    // imports (currently: the built-in sine table, synthetically generated;
    // everything else gets its address resolved but no real backing data --
    // fine for machines that don't use samples/RAM services), and zeroes
    // one track's Y-memory workset. Calls INIT once.
    bool install(const MdsPackage& package, std::string& error);

    // Writes a parameter directly as its already-converted DSP value (Q23,
    // raw14, etc. -- see MdsTransform.h) into Y-memory slot 1..8. Slot 0 and
    // slots beyond the package's own `params` size are free scratch, same
    // idea as the real 64-word workset -- useful for poking state the
    // machine doesn't expose as an official knob yet.
    void set_workset_word(unsigned slot, uint32_t value);
    uint32_t workset_word(unsigned slot) const;

    bool trigger(std::string& error);
    bool render_block(float* out32, std::string& error);  // writes 32 samples

    // Loads the "near" (one track back) and "far" (two/three tracks back)
    // neighbour-simulation signals from 16-bit PCM WAV files, looping each
    // independently once it runs out. Call any time after install(), before
    // the renders that should hear it. Either path may be empty to leave
    // that signal silent (zeroed) -- this is how a plain CLI invocation
    // behaves if it never calls this at all: no neighbour signal, same as
    // before this feature existed. Returns false (with `error` set) only on
    // a real load failure (bad/missing file) -- an empty path is not one.
    bool set_neighbour_wavs(const std::string& far_path, const std::string& near_path, std::string& error);

private:
    bool call(uint32_t entry, std::string& error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mdsh
