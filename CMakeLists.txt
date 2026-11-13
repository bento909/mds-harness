cmake_minimum_required(VERSION 3.15)
project(mds-harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to a dsp56300 checkout (https://github.com/dsp56300/dsp56300, GPLv3).
# Defaults to this repo's OWN vendored copy (vendor/dsp56300 -- just the
# dsp56kEmu/dsp56kBase/asmjit subset actually linked against, plus the few
# other subdirectories dsp56300's own source/CMakeLists.txt unconditionally
# add_subdirectory()s, ~11MB total, not the full ~280MB upstream checkout
# with its GUI debugger/plugin-wrapper/test-tooling this project never
# uses) -- so a fresh clone of mds-harness builds standalone, no sibling
# checkout required. Override with -DDSP56300_DIR=... to point at a real
# upstream checkout instead (e.g. to pick up newer emulator fixes).
if(NOT DEFINED DSP56300_DIR)
    set(DSP56300_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dsp56300")
endif()

if(WIN32)
    # dsp56kBase's mmuhelper.cpp uses VirtualAlloc2/MapViewOfFile3 (Windows
    # 10 1803+ APIs) that this MinGW toolchain's headers gate behind a
    # newer _WIN32_WINNT than its own default -- without this, the build
    # fails with "was not declared in this scope". Harmless on MSVC too.
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# The JIT (asmjit's x64 backend) crashes under this MinGW build -- a call
# through the engine's jsr call-stub ends up jumping to the stub's own
# address as if it were native code (confirmed via gdb backtrace: PC lands
# exactly on the stub). The plain interpreter doesn't have this problem and
# gives correct results; this engine never needed JIT speed anyway (it's
# an offline/dev-time render tool, not live audio yet). Revisit only if a
# future milestone genuinely needs the speed and this turns out to be a
# real, reportable upstream bug rather than an environment quirk.
set(DSP56K_FORCE_INTERPRETER ON CACHE BOOL "" FORCE)

if(NOT EXISTS "${DSP56300_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "dsp56300 not found at ${DSP56300_DIR}. Clone "
        "https://github.com/dsp56300/dsp56300 there, or pass "
        "-DDSP56300_DIR=/path/to/dsp56300.")
endif()

# dsp56kEmu depends on dsp56kBase and asmjit (see its own CMakeLists'
# target_link_libraries); pulling in the whole source tree (same as
# dsp56300's own top-level CMakeLists does) resolves all of that correctly
# rather than cherry-picking targets by hand. A few extra tools (the
# disassembler, test runner) get configured as a result but aren't used.
add_subdirectory("${DSP56300_DIR}" dsp56300_build)

add_executable(mds-harness
    src/main.cpp
    src/MdsPackage.cpp
    src/MdsTransform.cpp
    src/MachineEngine.cpp
    src/WavWriter.cpp
    src/WavReader.cpp
)
target_include_directories(mds-harness PRIVATE include)
target_link_libraries(mds-harness PRIVATE dsp56kEmu)

if(MINGW)
    # Statically link the MinGW runtime so the exe runs in any terminal --
    # without this, it needs MinGW's bin/ on PATH for libstdc++-6.dll etc.
    target_link_options(mds-harness PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
