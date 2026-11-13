#pragma once

#include "MdsPackage.h"

#include <cstdint>

// The 9 parameter transforms a raw 0..127 dial setting goes through before
// the DSP ever sees it, per the public spec (jmamma/MDS,
// docs/MDX_MachineDumpStandard.txt, "Parameter conversion and control
// style"). Re-derived directly from that table, not copied from
// mds-tools' own implementation -- see README.md "License / dependencies".
namespace mdsh {

// `raw` is always 0..127, same input range regardless of conversion (a
// bipolar dial just treats 64 as its own centre, per the spec).
// `min_value` only matters for conversion 6 (index); `bpm` only for
// conversion 8 (tempo); both ignored otherwise.
uint32_t convert_param(uint8_t conversion, uint8_t raw, uint8_t min_value = 0, double bpm = 120.0);

}  // namespace mdsh
