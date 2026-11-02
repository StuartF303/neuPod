// SPDX-License-Identifier: MPL-2.0
// Compile unit for the vendored single-header decoders (third party: compiled without the project warning flags).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "codec_dr_config.h"

#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_libs/dr_mp3.h"
