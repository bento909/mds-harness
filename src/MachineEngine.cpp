#include "MachineEngine.h"
#include "WavReader.h"

#include "dsp56kBase/logging.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

#include <cmath>

namespace mdsh {
namespace {

// This engine's own internal memory map -- addresses chosen fresh for this
// project, not reused from mds-tools' certifier (see README.md "License /
// dependencies"). Everything sits well under kMemSize/2 so nothing needs
// the Memory class's "bridged external memory" concept at all.
// Matches mds-tools' own certifier sizing exactly (proven to work), rather
// than guessing at the simpler single-arg Memory constructor's behavior.
constexpr uint32_t kMemSize = 0x200000;
constexpr uint32_t kBridge = 0x100000;
constexpr uint32_t kProgramBase = 0x000000;    // where the machine's own program loads
constexpr uint32_t kCallStub = 0x01f000;       // P-memory: jsr <entry>
constexpr uint32_t kReturnSentinel = 0x01f002; // P-memory: nop, lands here after rts
constexpr uint32_t kWorkset = 0x000000;        // Y-memory: this track's 64-word state
constexpr uint32_t kWorksetWords = 64;
constexpr uint32_t kOutputBuffer = 0x000100;   // Y-memory: 32-word RENDER output, circular
constexpr uint32_t kBlock = 32;
constexpr uint32_t kSineTable = 0x002000;      // X-memory: synthetic built-in sine table
constexpr uint32_t kSineWords = 32768;

// TRACK_BUFFERS (spec symbol 6, "TRACKMEM"): a real firmware allocation on
// actual hardware, one reserved region per track, far away from any other
// fixed address. This engine used to resolve it (and every import besides
// the sine table) to address 0 -- harmless for a machine that never
// actually uses the import, but for a machine like NFX-BO that computes
// "my TRACKMEM base = track_number*stride + this_import" and then writes
// captured audio INTO that region every block, address 0 silently landed
// right on top of kWorkset (0-63) -- i.e. the machine's own parameter
// values (NBAL, MIX, etc.) were getting overwritten by its own captured
// samples mid-render, making every knob look like a no-op after the first
// capture. Confirmed via a debug probe (workset[9], where NFX-BO's own
// INIT stores its computed TRACKMEM base, really did read back as 0).
// Fixed by giving it a real, exclusive address with generous headroom --
// 4096 words comfortably covers NFX-BO's own 1536-word BUFLEN with room
// to spare for any other machine using this same idiom.
constexpr uint32_t kTrackMemory = 0x001000;
constexpr uint32_t kTrackMemoryWords = 4096;

// Neighbour-machine simulation (spec section 3, "Neighbour machines"):
// Y:$07F0 is a SPEC-MANDATED fixed address, not something this engine can
// relocate -- it must hold the address of a 4-word descriptor table
// (own buffer, one/two/three tracks back). An NFX machine with nothing
// real to read from would just process silence, which tells you nothing
// about whether it actually works -- so this engine fabricates two fake
// "neighbour" buffers (near = one track back, far = two tracks back) and
// feeds them from loaded WAV files, each looping independently. The
// three-tracks-back slot stays silent -- it's wired into the descriptor
// table (so a machine that reads it gets real silence, not garbage), but
// has no WAV input of its own yet.
constexpr uint32_t kDescriptorPtr = 0x0007f0;
constexpr uint32_t kDescriptorTable = 0x000800;
constexpr uint32_t kNeighbourBuf1 = 0x000820;  // one track back ("near")
constexpr uint32_t kNeighbourBuf2 = 0x000840;  // two tracks back ("far")
constexpr uint32_t kNeighbourBuf3 = 0x000860;  // three tracks back -- always silent for now, see render_block()
constexpr uint32_t kJsrLong = 0x0bf080u;
constexpr uint32_t kMask24 = 0x00ffffffu;
constexpr uint64_t kInstructionBound = 2'000'000ull;

uint32_t resource_base(uint8_t symbol) {
    switch (symbol) {
        case kSymSine: return kSineTable;
        case kSymTrackMemory: return kTrackMemory;
        default: return 0;  // rings/sample info/etc: address resolves, data stays zeroed
    }
}

}  // namespace

struct MachineEngine::Impl {
    dsp56k::DefaultMemoryValidator validator;
    std::unique_ptr<dsp56k::Memory> memory;
    dsp56k::Peripherals56303 peripherals_x;
    dsp56k::PeripheralsNop peripherals_y;
    std::unique_ptr<dsp56k::DSP> dsp;
    uint32_t trig_entry = 0;
    uint32_t render_entry = 0;
    std::vector<int32_t> far_samples;   // two/three tracks back
    std::vector<int32_t> near_samples;  // one track back
    size_t far_pos = 0;
    size_t near_pos = 0;
};

MachineEngine::MachineEngine() : impl_(std::make_unique<Impl>()) {
    Logging::setLogFunc([](const std::string&) {});
    impl_->memory = std::make_unique<dsp56k::Memory>(impl_->validator, kMemSize, kMemSize, kBridge);
    impl_->dsp = std::make_unique<dsp56k::DSP>(*impl_->memory, &impl_->peripherals_x, &impl_->peripherals_y);
    // Without this, SP (and every other register) starts undefined --
    // the call-stub's jsr pushes a return address onto a garbage stack
    // pointer, which segfaults the moment anything calls rts.
    impl_->dsp->resetHW();
}

MachineEngine::~MachineEngine() = default;

namespace {
void set_mem(dsp56k::Memory& mem, dsp56k::EMemArea area, uint32_t addr, uint32_t value) {
    mem.set(area, addr, value & kMask24);
}
void set_reg24(dsp56k::DSP& dsp, dsp56k::EReg reg, uint32_t value) {
    dsp56k::TReg24 word;
    word.var = static_cast<int32_t>(value & kMask24);
    dsp.writeReg(reg, word);
}
}  // namespace

bool MachineEngine::install(const MdsPackage& package, std::string& error) {
    dsp56k::Memory& mem = *impl_->memory;

    size_t next_import = 0;
    for (size_t i = 0; i < package.program.size(); ++i) {
        uint32_t word = package.program[i];
        if (package.relocated[i]) word = (word + kProgramBase) & kMask24;
        if (next_import < package.imports.size() && package.imports[next_import].word == i) {
            const MdsImport& item = package.imports[next_import++];
            if (item.kind == 5) {
                // A callable service -- not implemented in this engine yet.
                // Point it at a bare RTS so a call returns harmlessly
                // instead of crashing; the machine just won't get real
                // sample/RAM-service behavior.
                static constexpr uint32_t kServiceStub = 0x01f010;
                set_mem(mem, dsp56k::MemArea_P, kServiceStub, 0x00000c);  // rts
                word = kServiceStub;
            } else {
                word = (resource_base(item.symbol) + word) & kMask24;
            }
        }
        set_mem(mem, dsp56k::MemArea_P, static_cast<uint32_t>(kProgramBase + i), word);
    }
    set_mem(mem, dsp56k::MemArea_P, kReturnSentinel, 0);  // nop

    // Synthetic built-in sine table (X-memory, 32768 words), if imported.
    bool needs_sine = false;
    for (const MdsImport& item : package.imports)
        if (item.kind == 1 && item.symbol == kSymSine) needs_sine = true;
    if (needs_sine) {
        for (uint32_t i = 0; i < kSineWords; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / kSineWords;
            const int32_t sample = static_cast<int32_t>(std::lround(std::sin(angle) * 8388607.0));
            set_mem(mem, dsp56k::MemArea_X, kSineTable + i, static_cast<uint32_t>(sample));
        }
    }

    for (uint32_t i = 0; i < kWorksetWords; ++i) set_mem(mem, dsp56k::MemArea_Y, kWorkset + i, 0);
    for (uint32_t i = 0; i < kBlock; ++i) set_mem(mem, dsp56k::MemArea_Y, kOutputBuffer + i, 0);

    bool needs_trackmem = false;
    for (const MdsImport& item : package.imports)
        if (item.kind != 5 && item.symbol == kSymTrackMemory) needs_trackmem = true;
    if (needs_trackmem)
        for (uint32_t i = 0; i < kTrackMemoryWords; ++i) set_mem(mem, dsp56k::MemArea_Y, kTrackMemory + i, 0);

    // Neighbour descriptor table -- addresses only; render_block() fills
    // the three fake buffers with a fresh test tone every block.
    set_mem(mem, dsp56k::MemArea_Y, kDescriptorTable + 0, kOutputBuffer);
    set_mem(mem, dsp56k::MemArea_Y, kDescriptorTable + 1, kNeighbourBuf1);
    set_mem(mem, dsp56k::MemArea_Y, kDescriptorTable + 2, kNeighbourBuf2);
    set_mem(mem, dsp56k::MemArea_Y, kDescriptorTable + 3, kNeighbourBuf3);
    set_mem(mem, dsp56k::MemArea_Y, kDescriptorPtr, kDescriptorTable);
    impl_->far_pos = 0;
    impl_->near_pos = 0;

    dsp56k::DSP& dsp = *impl_->dsp;
    set_reg24(dsp, dsp56k::Reg_R6, kWorkset);
    set_reg24(dsp, dsp56k::Reg_M6, kMask24);  // linear addressing
    set_reg24(dsp, dsp56k::Reg_N6, 0);
    set_reg24(dsp, dsp56k::Reg_R7, kOutputBuffer);
    set_reg24(dsp, dsp56k::Reg_M7, kBlock - 1);  // circular, modulo 32
    set_reg24(dsp, dsp56k::Reg_N7, 0);

    dsp.clearOpcodeCache();

    impl_->trig_entry = static_cast<uint32_t>(kProgramBase + package.trig_offset);
    impl_->render_entry = static_cast<uint32_t>(kProgramBase + package.render_offset);

    return call(static_cast<uint32_t>(kProgramBase + package.init_offset), error);
}

void MachineEngine::set_workset_word(unsigned slot, uint32_t value) {
    set_mem(*impl_->memory, dsp56k::MemArea_Y, kWorkset + slot, value);
}

uint32_t MachineEngine::workset_word(unsigned slot) const {
    return impl_->memory->get(dsp56k::MemArea_Y, kWorkset + slot) & kMask24;
}

bool MachineEngine::trigger(std::string& error) {
    return call(impl_->trig_entry, error);
}

bool MachineEngine::render_block(float* out32, std::string& error) {
    // Refresh the fake neighbour buffers with the next 32 samples from the
    // loaded near/far WAVs BEFORE calling RENDER, so a neighbour/NFX
    // machine reading "tracks back" gets a real signal. Each of near/far
    // loops independently once it reaches the end of its own buffer. If a
    // buffer was never loaded (set_neighbour_wavs() not called, or called
    // with an empty path), that signal is just silence -- the same
    // behavior this engine had before this feature existed.
    {
        dsp56k::Memory& mem = *impl_->memory;
        const std::vector<int32_t>& far = impl_->far_samples;
        const std::vector<int32_t>& near = impl_->near_samples;
        for (uint32_t i = 0; i < kBlock; ++i) {
            uint32_t far_word = 0;
            if (!far.empty()) {
                far_word = static_cast<uint32_t>(far[impl_->far_pos]) & kMask24;
                impl_->far_pos = (impl_->far_pos + 1) % far.size();
            }
            uint32_t near_word = 0;
            if (!near.empty()) {
                near_word = static_cast<uint32_t>(near[impl_->near_pos]) & kMask24;
                impl_->near_pos = (impl_->near_pos + 1) % near.size();
            }
            set_mem(mem, dsp56k::MemArea_Y, kNeighbourBuf1 + i, near_word);
            set_mem(mem, dsp56k::MemArea_Y, kNeighbourBuf2 + i, far_word);
            // kNeighbourBuf3 (three tracks back) is left at zero rather than
            // duplicating "far" into it -- that was never a real third
            // signal, just a stand-in. Give it a WAV of its own when this
            // engine supports a true three-track input; until then, silence
            // here is honest and duplicating "far" was not.
        }
    }

    if (!call(impl_->render_entry, error)) return false;
    dsp56k::Memory& mem = *impl_->memory;
    for (uint32_t i = 0; i < kBlock; ++i) {
        uint32_t word = mem.get(dsp56k::MemArea_Y, kOutputBuffer + i) & kMask24;
        int32_t signed_value = static_cast<int32_t>(word);
        if (word & 0x800000u) signed_value -= static_cast<int32_t>(0x1000000u);
        out32[i] = static_cast<float>(signed_value) / 8388608.0f;
    }
    return true;
}

bool MachineEngine::set_neighbour_wavs(const std::string& far_path, const std::string& near_path, std::string& error) {
    if (!far_path.empty()) {
        std::vector<int32_t> samples;
        if (!WavReader::load(far_path, samples, error)) return false;
        impl_->far_samples = std::move(samples);
        impl_->far_pos = 0;
    }
    if (!near_path.empty()) {
        std::vector<int32_t> samples;
        if (!WavReader::load(near_path, samples, error)) return false;
        impl_->near_samples = std::move(samples);
        impl_->near_pos = 0;
    }
    return true;
}

bool MachineEngine::call(uint32_t entry, std::string& error) {
    dsp56k::DSP& dsp = *impl_->dsp;
    dsp56k::Memory& mem = *impl_->memory;

    set_mem(mem, dsp56k::MemArea_P, kCallStub, kJsrLong);
    set_mem(mem, dsp56k::MemArea_P, kCallStub + 1, entry);
    dsp.clearOpcodeCache(kCallStub);
    dsp.clearOpcodeCache(kCallStub + 1);

    dsp.setPC(kCallStub);
    uint64_t steps = 0;
    for (;;) {
        const uint32_t pc = dsp.getPC().toWord() & kMask24;
        if (pc == kReturnSentinel) return true;
        if (++steps > kInstructionBound) {
            error = "ran more than the instruction bound without returning (entry $" + std::to_string(entry) + ")";
            return false;
        }
        dsp.exec();
    }
}

}  // namespace mdsh
