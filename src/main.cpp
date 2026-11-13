#include "MachineEngine.h"
#include "MdsPackage.h"
#include "MdsTransform.h"
#include "WavWriter.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Parses one --trig-at argument: "500" or "500:NBAL=80,RATE=40".
bool parse_trig_at(const std::string& arg, uint32_t& block, std::map<std::string, uint8_t>& plocks, std::string& error) {
    const size_t colon = arg.find(':');
    const std::string block_part = (colon == std::string::npos) ? arg : arg.substr(0, colon);
    try {
        block = static_cast<uint32_t>(std::stoul(block_part));
    } catch (const std::exception&) {
        error = "--trig-at has an invalid block number: '" + arg + "'";
        return false;
    }
    plocks.clear();
    if (colon == std::string::npos) return true;

    std::string rest = arg.substr(colon + 1);
    size_t pos = 0;
    while (pos < rest.size()) {
        const size_t comma = rest.find(',', pos);
        const std::string entry = rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const size_t eq = entry.find('=');
        if (eq == std::string::npos || entry.empty()) {
            error = "--trig-at plock entry must be NAME=VALUE, got '" + entry + "' in '" + arg + "'";
            return false;
        }
        const std::string name = upper(entry.substr(0, eq));
        int value = 0;
        try {
            value = std::stoi(entry.substr(eq + 1));
        } catch (const std::exception&) {
            error = "--trig-at plock value for " + name + " is not a number in '" + arg + "'";
            return false;
        }
        if (value < 0 || value > 127) {
            error = "--trig-at plock value for " + name + " must be 0..127 in '" + arg + "'";
            return false;
        }
        plocks[name] = static_cast<uint8_t>(value);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}

void print_usage() {
    std::cout <<
        "mds-harness render <machine.mds> -o <out.wav> [options]\n"
        "\n"
        "Options:\n"
        "  --param NAME=VALUE   Set a parameter (0..127) by its 4-char name. Repeatable.\n"
        "  --trig               Trigger the machine before rendering (at block 0).\n"
        "  --trig-at N[:NAME=V,...]   Trigger at block N -- repeatable, for\n"
        "                       simulating a sequencer's step pattern across a\n"
        "                       longer render (see tools/serve.py's step grid).\n"
        "                       The optional NAME=V list is a parameter lock\n"
        "                       (\"plock\"): NAME is overridden to raw value V\n"
        "                       (0..127) for this one trig only, then reverts to\n"
        "                       its --param/default value at the next trig point\n"
        "                       that doesn't also lock it.\n"
        "  --blocks N           Number of 32-sample RENDER blocks to render (default 100).\n"
        "  --sample-rate N      Sample rate stamped into the WAV (default 32000 -- the\n"
        "                       Machinedrum's exact rate isn't confirmed anywhere in this\n"
        "                       project; this is a reasonable estimate, not a fact).\n"
        "  --bpm N              Tempo for any \"tempo\"-conversion parameter (default 120).\n"
        "  --list-params        Print the machine's 8 parameter slots and exit.\n"
        "  --list-params-json   Same, as JSON (for scripting -- see tools/serve.py).\n"
        "  --neighbour-far F    16-bit PCM WAV to feed a neighbour/NFX machine's\n"
        "                       two/three-tracks-back buffers (loops if shorter than\n"
        "                       the render). Omit for silence -- there's no bundled\n"
        "                       default at this layer; tools/serve.py supplies one.\n"
        "  --neighbour-near F   Same, for the one-track-back buffer.\n";
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

struct Options {
    std::string mds_path;
    std::string wav_path;
    std::map<std::string, uint8_t> params;
    bool trig = false;
    // block -> {param name: raw plock value 0..127}; an empty inner map
    // means "plain trig, no plocks at this step."
    std::map<uint32_t, std::map<std::string, uint8_t>> trig_at_blocks;
    uint32_t blocks = 100;
    uint32_t sample_rate = 32000;
    double bpm = 120.0;
    bool list_params = false;
    bool list_params_json = false;
    std::string neighbour_far_wav;
    std::string neighbour_near_wav;
};

bool parse_args(int argc, char** argv, Options& opt, std::string& error) {
    if (argc < 2) {
        error = "missing command";
        return false;
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args[0] != "render") {
        error = "unknown command '" + args[0] + "' (only 'render' exists so far)";
        return false;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-o" && i + 1 < args.size()) {
            opt.wav_path = args[++i];
        } else if (a == "--param" && i + 1 < args.size()) {
            const std::string& kv = args[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                error = "--param expects NAME=VALUE, got '" + kv + "'";
                return false;
            }
            const std::string name = upper(kv.substr(0, eq));
            const int value = std::stoi(kv.substr(eq + 1));
            if (value < 0 || value > 127) {
                error = "--param value for " + name + " must be 0..127";
                return false;
            }
            opt.params[name] = static_cast<uint8_t>(value);
        } else if (a == "--trig") {
            opt.trig = true;
        } else if (a == "--trig-at" && i + 1 < args.size()) {
            // Repeatable: sequencer-style multi-trigger renders, one per
            // enabled step, each optionally carrying its own parameter
            // locks ("plocks"). A trig at block 0 with no plocks is
            // equivalent to --trig; both just mean "call trigger() right
            // before that block's render_block()".
            uint32_t block = 0;
            std::map<std::string, uint8_t> plocks;
            if (!parse_trig_at(args[++i], block, plocks, error)) return false;
            opt.trig_at_blocks[block] = plocks;
        } else if (a == "--blocks" && i + 1 < args.size()) {
            opt.blocks = static_cast<uint32_t>(std::stoul(args[++i]));
        } else if (a == "--sample-rate" && i + 1 < args.size()) {
            opt.sample_rate = static_cast<uint32_t>(std::stoul(args[++i]));
        } else if (a == "--bpm" && i + 1 < args.size()) {
            opt.bpm = std::stod(args[++i]);
        } else if (a == "--list-params") {
            opt.list_params = true;
        } else if (a == "--list-params-json") {
            opt.list_params = true;
            opt.list_params_json = true;
        } else if (a == "--neighbour-far" && i + 1 < args.size()) {
            opt.neighbour_far_wav = args[++i];
        } else if (a == "--neighbour-near" && i + 1 < args.size()) {
            opt.neighbour_near_wav = args[++i];
        } else if (opt.mds_path.empty() && a.size() > 0 && a[0] != '-') {
            opt.mds_path = a;
        } else {
            error = "unrecognized argument '" + a + "'";
            return false;
        }
    }
    if (opt.mds_path.empty()) {
        error = "missing <machine.mds> path";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    std::string error;
    if (!parse_args(argc, argv, opt, error)) {
        std::cerr << "error: " << error << "\n\n";
        print_usage();
        return 1;
    }

    mdsh::MdsPackage package;
    if (!mdsh::MdsPackage::load(opt.mds_path, package, error)) {
        std::cerr << "error loading " << opt.mds_path << ": " << error << "\n";
        return 1;
    }

    if (opt.list_params) {
        if (opt.list_params_json) {
            std::cout << "{\"tonal\":" << (package.tonal ? "true" : "false")
                      << ",\"neighbour\":" << (package.neighbour ? "true" : "false") << ",\"params\":[";
            bool first = true;
            for (size_t i = 0; i < package.params.size(); ++i) {
                const mdsh::MdsParam& p = package.params[i];
                if (!p.used()) continue;
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"slot\":" << (i + 1) << ",\"name\":\"" << json_escape(p.name) << "\",\"default\":"
                          << int(p.default_value) << ",\"min\":" << int(p.min_value) << ",\"max\":" << int(p.max_value)
                          << ",\"conversion\":" << int(p.conversion) << "}";
            }
            std::cout << "]}\n";
            return 0;
        }
        std::cout << opt.mds_path << " parameters:\n";
        for (size_t i = 0; i < package.params.size(); ++i) {
            const mdsh::MdsParam& p = package.params[i];
            if (!p.used()) continue;
            std::cout << "  " << (i + 1) << ". " << p.name << "  default=" << int(p.default_value)
                      << " range=[" << int(p.min_value) << ".." << int(p.max_value) << "]"
                      << " conversion=" << int(p.conversion) << "\n";
        }
        return 0;
    }

    if (opt.wav_path.empty()) {
        std::cerr << "error: -o <out.wav> is required (or pass --list-params)\n";
        return 1;
    }

    mdsh::MachineEngine engine;
    if (!engine.install(package, error)) {
        std::cerr << "error installing machine: " << error << "\n";
        return 1;
    }

    if (!opt.neighbour_far_wav.empty() || !opt.neighbour_near_wav.empty()) {
        if (!engine.set_neighbour_wavs(opt.neighbour_far_wav, opt.neighbour_near_wav, error)) {
            std::cerr << "error loading neighbour WAV: " << error << "\n";
            return 1;
        }
    }

    // Parameters default to each slot's own declared default; apply any
    // --param overrides by name, up to all 8 official slots (set_workset_word
    // can also address slot 0 or slots 9.. directly if ever needed, but
    // --param only covers the named/official ones for now). Also record
    // each param's slot/conversion/base value so the render loop below
    // can revert a plocked param back to this base value at the next
    // trig point that doesn't also lock it.
    struct ParamInfo {
        unsigned slot;
        uint8_t conversion;
        uint8_t min_value;
        uint32_t base_value;  // already converted
    };
    std::map<std::string, ParamInfo> param_info;
    for (size_t i = 0; i < package.params.size(); ++i) {
        const mdsh::MdsParam& p = package.params[i];
        if (!p.used()) continue;
        uint8_t raw = p.default_value;
        const auto it = opt.params.find(upper(p.name));
        if (it != opt.params.end()) raw = it->second;
        const uint32_t value = mdsh::convert_param(p.conversion, raw, p.min_value, opt.bpm);
        engine.set_workset_word(static_cast<unsigned>(i + 1), value);
        param_info[upper(p.name)] = ParamInfo{static_cast<unsigned>(i + 1), p.conversion, p.min_value, value};
    }

    for (const auto& [name, raw] : opt.params) {
        if (param_info.find(name) == param_info.end())
            std::cerr << "warning: '" << name << "' is not one of this machine's parameters, ignored\n";
    }
    for (const auto& [block, plocks] : opt.trig_at_blocks) {
        for (const auto& [name, raw] : plocks) {
            if (param_info.find(name) == param_info.end())
                std::cerr << "warning: plock at block " << block << " names '" << name << "', not one of this machine's parameters, ignored\n";
        }
    }

    mdsh::WavWriter wav;
    if (!wav.open(opt.wav_path, opt.sample_rate, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    // Embed exactly what produced this render as a WAV comment chunk
    // (see WavWriter::set_comment) -- so the file alone, inspected later
    // (by a dev, or an agent reading it off disk), answers "what knob
    // values/trig pattern/neighbour WAVs made this sound the way it does?"
    {
        std::ostringstream c;
        c << "{\"mds\":\"" << json_escape(opt.mds_path) << "\",\"bpm\":" << opt.bpm
          << ",\"sample_rate\":" << opt.sample_rate << ",\"blocks\":" << opt.blocks << ",\"params\":{";
        bool first = true;
        for (const auto& [name, info] : param_info) {
            if (!first) c << ",";
            first = false;
            const auto it = opt.params.find(name);
            const int raw_value = (it != opt.params.end())
                ? static_cast<int>(it->second)
                : -1;  // -1 means "machine's own declared default, not overridden"
            c << "\"" << json_escape(name) << "\":{\"raw\":" << raw_value << ",\"value\":" << info.base_value << "}";
        }
        c << "},\"trig_at\":[";
        first = true;
        for (const auto& [blk, plocks] : opt.trig_at_blocks) {
            if (!first) c << ",";
            first = false;
            c << "{\"block\":" << blk << ",\"plocks\":{";
            bool pfirst = true;
            for (const auto& [name, raw] : plocks) {
                if (!pfirst) c << ",";
                pfirst = false;
                c << "\"" << json_escape(name) << "\":" << int(raw);
            }
            c << "}}";
        }
        c << "]";
        if (!opt.neighbour_far_wav.empty()) c << ",\"neighbour_far\":\"" << json_escape(opt.neighbour_far_wav) << "\"";
        if (!opt.neighbour_near_wav.empty()) c << ",\"neighbour_near\":\"" << json_escape(opt.neighbour_near_wav) << "\"";
        c << "}";
        wav.set_comment(c.str());
    }

    if (opt.trig) opt.trig_at_blocks[0];  // insert-if-absent; operator[] on a map does this

    float block[32];
    for (uint32_t b = 0; b < opt.blocks; ++b) {
        const auto trig_it = opt.trig_at_blocks.find(b);
        if (trig_it != opt.trig_at_blocks.end()) {
            // Apply this step's plocks, and revert every OTHER param back
            // to its base value -- matches real hardware: a plock holds
            // for exactly the step that sets it, then the next trig point
            // (locked or not) decides the value again from scratch.
            const std::map<std::string, uint8_t>& plocks = trig_it->second;
            for (const auto& [name, info] : param_info) {
                const auto plock_it = plocks.find(name);
                const uint32_t value = (plock_it != plocks.end())
                    ? mdsh::convert_param(info.conversion, plock_it->second, info.min_value, opt.bpm)
                    : info.base_value;
                engine.set_workset_word(info.slot, value);
            }
            if (!engine.trigger(error)) {
                std::cerr << "error triggering machine at block " << b << ": " << error << "\n";
                wav.close(error);
                return 1;
            }
        }
        if (!engine.render_block(block, error)) {
            std::cerr << "error in RENDER (block " << b << "): " << error << "\n";
            wav.close(error);
            return 1;
        }
        wav.write(block, 32);
    }

    if (!wav.close(error)) {
        std::cerr << "error finishing " << opt.wav_path << ": " << error << "\n";
        return 1;
    }

    std::cout << "wrote " << opt.wav_path << " (" << opt.blocks << " blocks, " << (opt.blocks * 32) << " samples at "
              << opt.sample_rate << " Hz)\n";
    return 0;
}
